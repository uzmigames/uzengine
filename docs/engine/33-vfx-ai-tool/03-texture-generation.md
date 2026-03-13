# VFX Texture Generation — ComfyUI + SDXL Local Pipeline

> **Status**: Planned (Phase 3.5)
> **Source**: `editor/vfx-ai-tool/texture_generator.ts`, ComfyUI REST API integration
> **References**: Stable Diffusion XL (model), ComfyUI (workflow automation), ControlNet (guided generation), SDXL-Turbo (fast inference)

## Purpose

The system generates particle textures **locally** using Stable Diffusion XL via ComfyUI (Python service). No cloud, no API keys, full control. Supports seamless tiling, ControlNet guidance, and batch generation during asset cooking. Textures are cached by prompt hash — identical prompts always reuse the same texture.

**Why ComfyUI + SDXL** (not "Nano Banana"):
- "Nano Banana" was misidentified branding for Google Gemini Flash
- Gemini Flash is cloud-only, proprietary, no local option
- ComfyUI + SDXL runs **100% locally**, free, open-source
- Full control over generation, no rate limits, no API quota

## Architecture

```
┌────────────────────────────────────┐
│  Engine requests texture           │
│  (effect description + params)     │
└────────────────┬───────────────────┘
                 │
                 ▼
        ┌────────────────────┐
        │ Prompt Engineering │
        │ + Control image    │
        │ (optional)         │
        └────────┬───────────┘
                 │
                 ▼
    ┌───────────────────────────────────┐
    │  ComfyUI REST API                 │
    │  (running locally on GPU machine) │
    │  POST /api/prompt                 │
    │  ├── Model: SDXL base             │
    │  ├── Sampler: DPM++ 2M Karras     │
    │  ├── Steps: 20–30                 │
    │  └── CFG scale: 7.5–8.5           │
    └────────┬──────────────────────────┘
             │
             ├─ VRAM usage: 10–12 GB      │
             ├─ Time: 2–5 seconds         │
             └─ Returns PNG as base64     │
                 │
                 ▼
    ┌────────────────────────────────┐
    │  Post-Processing               │
    │  ├── Optional: seamless tiling  │
    │  ├── Optional: alpha mask       │
    │  └── Save as PNG                │
    └────────┬───────────────────────┘
             │
             ▼
    ┌────────────────────────────────┐
    │  Asset Import & Cache           │
    │  ├── PNG → .uztexture           │
    │  ├── Cache key: xxHash64(prompt)│
    │  └── Register in VFS            │
    └────────────────────────────────┘
```

## Hardware Requirements

ComfyUI + SDXL runs locally. Minimum hardware:

| GPU | VRAM | Speed | Notes |
|-----|------|-------|-------|
| **RTX 2080 Ti** | 11 GB | ~8–10 sec/image | Older, works but slow |
| **RTX 3080** | 10 GB | ~4–6 sec/image | Good baseline |
| **RTX 4080** | 16 GB | ~2–3 sec/image | Fast |
| **RTX 4090** | 24 GB | ~1–2 sec/image | Best, also runs TRELLIS.2 meshes |
| **AMD 6900XT** | 16 GB | ~5–7 sec/image | Via HIP (ROCM) |

**Fallback**: If user doesn't have ComfyUI running, system falls back to procedural particle textures (engine-provided fallback atlas).

## ComfyUI Setup & Workflow

### Installation (One-time)

```bash
# Install ComfyUI (if not present)
git clone https://github.com/comfyui/ComfyUI
cd ComfyUI
pip install -r requirements.txt

# Install SDXL model (or download manually)
# Place in: ComfyUI/models/checkpoints/sd_xl_base_1.0.safetensors

# Install optional extensions
git clone https://github.com/canny/ComfyUI_ControlNet_Preprocessors \
    custom_nodes/
```

### REST API

ComfyUI exposes a REST API. System calls via HTTP POST:

```typescript
// TypeScript: Generate texture via ComfyUI
async function generateTextureViaComfyUI(
    prompt: string,
    negativePrompt: string,
    controlImage?: Buffer,  // Optional: reference image for ControlNet
    seed: number = 0
): Promise<Buffer> {
    const payload = {
        "1": {
            "inputs": {
                "ckpt_name": "sd_xl_base_1.0.safetensors"
            },
            "class_type": "CheckpointLoaderSimple"
        },
        "2": {
            "inputs": {
                "text": prompt,
                "clip": ["1", 0]
            },
            "class_type": "CLIPTextEncode"
        },
        "3": {
            "inputs": {
                "text": negativePrompt,
                "clip": ["1", 0]
            },
            "class_type": "CLIPTextEncode"
        },
        "4": {
            "inputs": {
                "width": 512,
                "height": 512,
                "length": 1,
                "batch_size": 1
            },
            "class_type": "EmptyLatentImage"
        },
        "5": {
            "inputs": {
                "seed": seed,
                "steps": 20,
                "cfg": 7.5,
                "sampler_name": "dpmpp_2m_karras",
                "scheduler": "karras",
                "denoise": 1.0,
                "model": ["1", 0],
                "positive": ["2", 0],
                "negative": ["3", 0],
                "latent_image": ["4", 0]
            },
            "class_type": "KSampler"
        },
        "6": {
            "inputs": {
                "samples": ["5", 0],
                "vae": ["1", 0]
            },
            "class_type": "VAEDecode"
        },
        "7": {
            "inputs": {
                "filename_prefix": "uz_generated",
                "images": ["6", 0]
            },
            "class_type": "SaveImage"
        }
    };

    // POST to ComfyUI queue
    const response = await fetch("http://localhost:8188/api/prompt", {
        method: "POST",
        body: JSON.stringify(payload)
    });

    const data = await response.json();
    const promptId = data.prompt_id;

    // Poll for completion
    let completed = false;
    let result: Buffer | null = null;

    while (!completed) {
        const statusResp = await fetch(`http://localhost:8188/api/history/${promptId}`);
        const history = await statusResp.json();

        if (history[promptId]?.outputs) {
            const outputs = history[promptId].outputs;
            if (outputs[7]) {  // SaveImage node
                const images = outputs[7].images;
                const filename = images[0].filename;

                // Download image
                const imgResp = await fetch(`http://localhost:8188/view?filename=${filename}`);
                result = Buffer.from(await imgResp.arrayBuffer());
                completed = true;
            }
        } else {
            // Still processing, wait a bit
            await new Promise(r => setTimeout(r, 500));
        }
    }

    return result!;
}
```

## Prompt Engineering for VFX Textures

Careful prompt design ensures quality results. System builds prompts with:

```
[quality keywords] [style] [effect description] [technical specs]

Examples:

"high quality, diffuse particle texture, bright orange campfire,
 soft smoke wisps, seamless tiling, transparent background"

"detailed texture, electric energy crackle, blue and purple plasma,
 smooth gradients, 512x512, no faces, abstract"

"vfx texture, water splash droplet, volumetric, depth of field,
 cinematic lighting, 4k, high resolution"
```

**Negative prompt** (always included):
```
"text, letters, faces, bodies, objects, artifacts, blur, noise,
 low quality, deformed, distorted"
```

**Common VFX texture categories**:

| Category | Prompt Pattern | Result |
|----------|----------------|--------|
| **Fire** | "bright orange/yellow fire, smoke wisps, soft edges, seamless" | Loopable fire particles |
| **Smoke** | "volumetric smoke clouds, white/gray, soft falloff, transparent" | Smoke billows |
| **Electric** | "blue crackling energy, plasma tendrils, glow, abstract" | Electric effect |
| **Magic** | "glowing magic particles, mystical aura, ethereal, soft" | Magic/holy effect |
| **Sparkles** | "golden sparkles, light flares, bokeh, transparent" | Spark effect |
| **Water** | "water droplets, splash, translucent, blue tint" | Water particles |
| **Dust** | "dust particles, volumetric, hazy, golden light" | Dust/sand effect |

## Seamless Tiling (Optional)

For loopable effects, system can generate seamless textures:

```typescript
async function generateSeamlessTexture(prompt: string): Promise<Buffer> {
    // Add tiling instruction to prompt
    const tilingPrompt = prompt + ", seamless tiling, wraparound edges";

    // ComfyUI workflow includes seamless tiling node
    const payload = {
        // ... (standard SDXL setup) ...
        "seamless": {
            "inputs": {
                "mode": "x+y",  // tile both axes
                "images": ["6", 0]  // VAE decode output
            },
            "class_type": "SeamlessTiling"
        }
    };

    // Similar fetch/poll as above
    return generateViaComfyUI(payload);
}
```

**Result**: Texture can be tiled infinitely without seams (important for looping particle effects).

## Batch Generation & Asset Cooking

During asset cooking (`uzbuild`), system can batch-generate textures for multiple effects:

```cpp
/// Batch texture generation for VFX effects.
/// Runs in parallel, caching results.
class VFXTextureBaker {
public:
    struct TextureRequest {
        std::string prompt;
        std::string output_path;  // res://vfx/textures/effect_name.png
        bool seamless = true;
        uint32_t seed = 0;
    };

    /// Generate multiple textures in parallel via ComfyUI.
    /// Returns count of successfully generated textures.
    int BakeTextures(
        const std::vector<TextureRequest>& requests,
        const std::string& comfyui_url = "http://localhost:8188"
    );

    /// Cache texture by prompt hash for future reuse.
    void CacheTexture(
        StringView prompt,
        const std::vector<uint8_t>& png_bytes
    );

    /// Check if texture already cached (skip generation).
    bool IsCached(StringView prompt) const;
};
```

Usage in build script:

```cpp
// During `zig build vfx-cook`:
VFXTextureBaker baker;
std::vector<TextureRequest> requests = {
    { "bright orange fire, seamless", "res://vfx/tex/fire.png", true, 42 },
    { "white smoke, volumetric, seamless", "res://vfx/tex/smoke.png", true, 43 },
    { "blue electric energy, seamless", "res://vfx/tex/electric.png", true, 44 },
};
int generated = baker.BakeTextures(requests, "http://localhost:8188");
UZ_LOG(VFX, Info, "Baked {} textures", generated);
```

## Caching Strategy

Textures cached by **prompt hash** to avoid regenerating identical textures:

```cpp
/// Generate texture with automatic caching.
Buffer GenerateTexture(StringView prompt) {
    uint64_t hash = xxHash64(prompt);
    std::string cache_key = fmt::format("{:016x}", hash);
    std::string cache_path = fmt::format("res://.cache/vfx/{}.uztexture", cache_key);

    // Check if already cached
    if (FileSystem::Exists(cache_path)) {
        return FileSystem::Read(cache_path);
    }

    // Generate via ComfyUI
    Buffer png_bytes = ComfyUI::Generate(prompt);

    // Import as .uztexture (compress, mipmap, etc.)
    AssetImporter::ImportTexture(png_bytes, cache_path, {
        .compression = TextureCompression::BC7,
        .generate_mipmaps = true
    });

    return FileSystem::Read(cache_path);
}
```

**Result**: If user creates 10 fire effects with same prompt, only 1 texture generated, reused 10 times.

## Integration with Asset Pipeline

Generated textures integrate with the VFX material system:

```json
{
    "format": "uzmaterial",
    "name": "fire_particle",

    "parameters": {
        "color_tint": { "type": "vec3", "default": [1, 0.6, 0] },
        "intensity": { "type": "float", "default": 1.0 }
    },

    "textures": {
        "diffuse": "generated/tex_fire_particle.uztexture"  // AI-generated
    },

    "shader": "generated/fire_particle.uzshader"  // Claude-generated
}
```

## Quality Comparison

SDXL-generated VFX textures vs. hand-painted:

| Metric | SDXL | Hand-Painted | Notes |
|--------|------|--------------|-------|
| **Speed** | 2–5 sec | 10–30 min | AI is 100–1000x faster |
| **Iteration** | Real-time (prompt → regenerate) | Hours per change | AI enables rapid iteration |
| **Consistency** | Good (seed-based) | Variable (artist skill) | Both good, different strengths |
| **Uniqueness** | Procedural (infinite variations) | Hand-crafted | AI can generate variations easily |
| **Cost** | ~$0.001/texture (GPU) | $50–200 (artist hours) | AI much cheaper |
| **Artistic control** | Prompt engineering | Full control | Artist has explicit control |
| **Quality** | 8/10 (very good for indie) | 9–10/10 (AAA) | SDXL good enough for most games |

**Verdict**: SDXL textures are production-quality for indie/AA games. AAA studios may hand-touch or refinish, but raw AI output is excellent.

## Fallback (No ComfyUI)

If ComfyUI not running, system falls back to procedural textures:

```cpp
/// Procedural fallback texture atlas (engine-provided).
class ProceduralVFXTextures {
public:
    /// Return pre-baked procedural texture for effect type.
    static Texture GetFallback(StringView effect_type) {
        static const std::unordered_map<std::string, Texture> atlas = {
            { "fire", GenerateFireTexture() },
            { "smoke", GenerateSmokeTexture() },
            { "spark", GenerateSparkTexture() },
            // ... etc
        };
        return atlas.at(effect_type);
    }
};

// Usage: if ComfyUI fails, use procedural
Buffer texture_bytes = ComfyUI::TryGenerate(prompt);
if (!texture_bytes) {
    texture_bytes = ProceduralVFXTextures::GetFallback(effect_type);
    UZ_LOG(VFX, Warning, "ComfyUI unavailable, using procedural fallback");
}
```

## Alternatives Evaluated

| System | Pros | Cons | Used? |
|--------|------|------|-------|
| **ComfyUI + SDXL** | Local, free, open, full control | Needs RTX 3080+, setup | ✅ Primary |
| **Flux.1** | Higher quality | Too much VRAM (40+ GB), newer | ⏳ Phase 4+ if available |
| **Scenario.gg** | Cloud-based, easy | Paid ($99/mo), proprietary | ❌ No |
| **Material Maker** | Procedural, deterministic | Limited variety for VFX | ⏳ Complementary in Phase 4 |
| **Automatic1111** | User-friendly UI | Less configurable than ComfyUI | ⏳ Alternative if preferred |

## Related Documentation

- `docs/engine/33-vfx-ai-tool/01-overview.md` — Overview of VFX AI system
- `docs/engine/33-vfx-ai-tool/02-shader-generation.md` — Claude shader generation
- `docs/engine/25-vfx/` — VFX system, materials, rendering
- `docs/engine/08-resource-system/` — Asset import pipeline
