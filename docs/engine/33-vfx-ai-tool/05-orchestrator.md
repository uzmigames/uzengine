# VFX AI Orchestrator — Central Coordination System

> **Status**: Planned (Phase 3–4)
> **Source**: `editor/vfx-ai-tool/vfx_orchestrator.ts`, `runtime/vfx-ai/vfx_orchestrator.cpp`
> **References**: IPC protocol, asset pipeline, shader compilation, texture import

## Purpose

The VFX AI Orchestrator is the central system that coordinates all three AI services (Claude, ComfyUI, TRELLIS.2) into a unified, end-to-end generation pipeline. It:

1. **Parses** natural language input into structured VFX parameters
2. **Routes** requests to appropriate services (shader → Claude, texture → ComfyUI, mesh → TRELLIS.2)
3. **Manages** parallel execution and error recovery
4. **Assembles** final `.uzvfx` definition from generated components
5. **Validates** entire effect (compiles shaders, imports textures, confirms schema)
6. **Enables iteration** — user feedback regenerates only changed components (fast feedback loop)

## Architecture

```
┌─────────────────────────────────────────────┐
│  VSCode Extension UI (TypeScript)           │
│  ├── VFX AI Tool panel                      │
│  ├── Natural language input or template sel │
│  └── Live preview viewport                  │
└────────────────────┬────────────────────────┘
                     │
                     ▼ (IPC: vfx-ai-generate)
    ┌────────────────────────────────────────┐
    │ VFX Orchestrator (C++ runtime layer)   │
    │                                        │
    │  1. Input Parsing                      │
    │     ├── Claude: describe → params      │
    │     └── Extract: colors, intensity,    │
    │         emitter count, physics         │
    │                                        │
    │  2. Service Routing                    │
    │     ├── Shaders: always Claude         │
    │     ├── Textures: ComfyUI (optional)   │
    │     └── Meshes: TRELLIS.2 (rare)       │
    │                                        │
    │  3. Parallel Dispatch                  │
    │     ├── Launch all services in parallel│
    │     ├── Monitor progress               │
    │     └── Error recovery on failure      │
    │                                        │
    │  4. Assembly                           │
    │     ├── Combine generated assets       │
    │     ├── Generate .uzvfx JSON           │
    │     └── Create materials               │
    │                                        │
    │  5. Validation                         │
    │     ├── DXC compile shaders            │
    │     ├── Import textures to VFS         │
    │     ├── Validate .uzvfx schema         │
    │     └── Register with asset manager    │
    │                                        │
    │  6. Iteration Loop                     │
    │     ├── Listen for user feedback       │
    │     ├── Diff generation request        │
    │     ├── Regenerate only changed parts  │
    │     └── Update preview in real-time    │
    └────────────────┬───────────────────────┘
                     │
                     ▼ (IPC: vfx-preview-update)
         ┌───────────────────────────┐
         │ Viewport Preview          │
         │ ├── Hot-load .uzvfx       │
         │ ├── Play/stop animation   │
         │ ├── Show parameter panel  │
         │ └── Enable iteration      │
         └───────────────────────────┘
```

## Input Parsing & Parameter Extraction

### Natural Language → Structured Parameters

User describes effect in natural language:
```
"bright blue energy explosion with electric crackling and lightning"
```

Claude parses this into structured JSON:

```cpp
/// Parse user description into VFX parameters.
struct VFXParameters {
    // Color & appearance
    Vec3 primary_color;           // Dominant color (RGB, 0–1)
    Vec3 secondary_color;         // Accent color
    float intensity;              // Brightness multiplier (1.0 = normal)
    std::string style;            // "fire", "electric", "magic", "water", etc.

    // Emission
    float emission_rate;          // Particles per second
    float particle_lifespan;      // Seconds
    Vec3 spawn_velocity;          // Direction and speed
    float velocity_spread;        // Random variation (0–1)

    // Physics
    Vec3 gravity;                 // Acceleration per second
    float damping;                // Velocity damping (0.9 = 90% per frame)
    bool collision_enabled;       // Physics collision?

    // Rendering
    int emitter_count;            // Single vs. multi-emitter
    std::string primary_render_type;  // "sprite", "mesh", "ribbon"
    bool has_glow;                // Additive glow?
    bool has_distortion;          // Distortion effect?

    // Advanced
    std::vector<std::string> keywords;  // ["electric", "explosion", "blue"]
};

/// Parse user input via Claude.
class InputParser {
public:
    VFXParameters Parse(StringView user_description) {
        std::string prompt = fmt::format(
            R"(
Parse this VFX effect description into a JSON structure:

Description: "{}"

Required JSON fields:
{{
    "primary_color": [R, G, B],      // 0–1
    "secondary_color": [R, G, B],
    "intensity": 1.0–2.0,
    "style": "fire|electric|magic|water|ice|holy",
    "emission_rate": 50–1000,
    "particle_lifespan": 0.5–5.0,
    "spawn_velocity": [x, y, z],
    "velocity_spread": 0.1–0.9,
    "gravity": [0, 0, z],
    "damping": 0.85–0.99,
    "collision_enabled": true|false,
    "emitter_count": 1–4,
    "primary_render_type": "sprite|mesh",
    "has_glow": true|false,
    "has_distortion": true|false,
    "keywords": ["word1", "word2", ...]
}}

Return ONLY valid JSON, no explanations.
            )",
            user_description
        );

        std::string json_response = CallClaudeAPI(prompt);
        return ParseJSON<VFXParameters>(json_response);
    }
};

// Usage:
InputParser parser;
auto params = parser.Parse("bright blue energy explosion with electric crackling");
// Result: primary_color=[0,0,1], emission_rate=500, style="electric", etc.
```

## Service Routing & Dispatch

Based on parsed parameters, route to services:

```cpp
/// Route VFX generation to appropriate services.
class ServiceRouter {
public:
    struct GenerationRequest {
        VFXParameters params;
        bool generate_shader = true;   // Always true
        bool generate_texture = true;  // Conditional
        bool generate_mesh = false;    // Rare
        std::string texture_style;     // "fire", "smoke", etc.
        std::string reference_image;   // For TRELLIS.2
    };

    /// Decide which services to invoke.
    GenerationRequest Route(const VFXParameters& params) {
        GenerationRequest req;
        req.params = params;

        // Shaders: ALWAYS Claude
        req.generate_shader = true;

        // Textures: ComfyUI if available and not explicitly disabled
        req.generate_texture = IsComfyUIAvailable() && params.style != "none";
        req.texture_style = MapStyleToPrompt(params.style);

        // Meshes: TRELLIS.2 only for mesh-based effects
        req.generate_mesh = (params.primary_render_type == "mesh") &&
                            IsTRELLIS2Available();

        // Reference image: user-provided or generate via SDXL
        if (req.generate_mesh && params.reference_image.empty()) {
            req.reference_image = GenerateReferenceImageViaSDXL(params);
        }

        return req;
    }

private:
    bool IsComfyUIAvailable() const {
        // Check if service is running
        return IsServiceHealthy("http://localhost:8188");
    }

    bool IsTRELLIS2Available() const {
        return IsServiceHealthy("http://localhost:8889");
    }

    std::string MapStyleToPrompt(StringView style) {
        static const std::unordered_map<std::string, std::string> map = {
            { "fire", "bright orange fire, smoke wisps, seamless, high quality" },
            { "electric", "blue crackling energy, plasma, glow, abstract" },
            { "magic", "mystical magical aura, ethereal, soft glow" },
            { "water", "water droplets, translucent, blue tint" },
            { "ice", "crystalline ice, cold blue, sparkles" },
        };
        auto it = map.find(style.data());
        return (it != map.end()) ? it->second : "abstract particles";
    }
};
```

## Parallel Generation & Assembly

All services execute in parallel (or conditionally):

```cpp
/// Orchestrate generation with parallel dispatch.
class VFXOrchestrator {
public:
    struct GenerationResult {
        bool success;
        ResourceHandle<VFXGraph> effect;  // Ready-to-use .uzvfx
        float total_time_sec;
        std::vector<std::string> warnings;
    };

    /// Generate complete VFX effect from description.
    /// Returns immediately with async handle if requested.
    GenerationResult Generate(
        StringView effect_description,
        bool async = false
    ) {
        // Parse input
        InputParser parser;
        auto params = parser.Parse(effect_description);

        // Route to services
        ServiceRouter router;
        auto req = router.Route(params);

        // Dispatch in parallel
        auto shader_task = std::async(std::launch::async, [&] {
            return GenerateShader(params);
        });

        auto texture_task = std::async(std::launch::async, [&] {
            return req.generate_texture ? GenerateTexture(req.texture_style)
                                        : Buffer();
        });

        auto mesh_task = std::async(std::launch::async, [&] {
            return req.generate_mesh ? GenerateMesh(req.reference_image)
                                     : nullptr;
        });

        // Wait for all tasks
        auto shader_result = shader_task.get();
        auto texture_result = texture_task.get();
        auto mesh_result = mesh_task.get();

        // Validate
        if (!shader_result.success) {
            return GenerationResult{ false, {}, 0, { shader_result.error } };
        }

        // Assemble .uzvfx
        auto uzvfx = AssembleEffect(params, shader_result, texture_result, mesh_result);

        // Import to VFS
        ImportEffectAssets(uzvfx, params);

        // Register with engine
        ResourceHandle<VFXGraph> effect_handle = AssetManager::Register(uzvfx);

        return GenerationResult{
            true,
            effect_handle,
            // ... timing
        };
    }

private:
    ShaderGenerationResult GenerateShader(const VFXParameters& params) {
        // Call Claude HLSL generator
        auto gen = std::make_unique<LLMShaderGenerator>();
        return gen->GenerateShader(DescribeForShader(params), params);
    }

    Buffer GenerateTexture(StringView style) {
        // Call ComfyUI
        auto gen = std::make_unique<TextureGenerator>();
        return gen->Generate(style);
    }

    Mesh GenerateMesh(StringView reference_image_path) {
        // Call TRELLIS.2
        auto gen = std::make_unique<TRELLIS2MeshGenerator>();
        auto image = FileSystem::ReadBinary(reference_image_path);
        return gen->GenerateMesh(image).glb_bytes;
    }
};
```

## Effect Assembly & Validation

Combine generated components into `.uzvfx`:

```cpp
/// Assemble complete VFX effect from generated components.
class EffectAssembler {
public:
    json AssembleEffect(
        const VFXParameters& params,
        const ShaderGenerationResult& shader,
        const Buffer& texture_bytes,
        const Mesh* mesh
    ) {
        json effect;
        effect["format"] = "uzvfx";
        effect["version"] = 1;
        effect["name"] = GenerateEffectName(params.style);
        effect["type"] = "emitter";

        // Spawn module
        effect["spawn"] = {
            { "rate", params.emission_rate },
            { "lifespan_min", params.particle_lifespan * 0.8f },
            { "lifespan_max", params.particle_lifespan * 1.2f },
            { "velocity", params.spawn_velocity },
            { "velocity_variation", params.velocity_spread },
        };

        // Update module
        effect["update"] = {
            { "gravity", params.gravity },
            { "damping", params.damping },
            { "collision", params.collision_enabled },
        };

        // Render module
        effect["render"] = {
            { "type", params.primary_render_type },
            { "material", "generated/mat_" + effect["name"].get<std::string>() },
            { "sort", "back_to_front" },
        };

        // Parameters
        json params_json;
        params_json["intensity"] = {
            { "type", "float" },
            { "default", params.intensity },
            { "min", 0.5f },
            { "max", 2.0f },
        };
        params_json["color_tint"] = {
            { "type", "vec3" },
            { "default", params.primary_color },
        };
        effect["parameters"] = params_json;

        return effect;
    }

    /// Validate generated effect.
    /// Compiles shader, imports texture, validates schema.
    bool ValidateEffect(const json& uzvfx) {
        // Compile shader
        std::string shader_path = GetShaderPath(uzvfx);
        if (!CompileShader(shader_path)) {
            UZ_LOG(VFX, Error, "Shader compilation failed");
            return false;
        }

        // Import texture
        std::string texture_path = GetTexturePath(uzvfx);
        if (!texture_path.empty()) {
            if (!ImportTexture(texture_path)) {
                UZ_LOG(VFX, Error, "Texture import failed");
                return false;
            }
        }

        // Validate schema
        if (!ValidateSchema(uzvfx, "uzvfx.schema.json")) {
            UZ_LOG(VFX, Error, ".uzvfx schema validation failed");
            return false;
        }

        return true;
    }
};
```

## Iteration & Refinement Loop

User provides feedback, system regenerates only changed components:

```cpp
/// Iteration system for rapid VFX refinement.
class IterationController {
public:
    struct IterationRequest {
        std::string feedback;  // "More fire", "Add glow", "Slower"
        ResourceHandle<VFXGraph> current_effect;
    };

    /// Parse feedback and determine what to regenerate.
    GenerationResult Iterate(const IterationRequest& req) {
        // Parse feedback
        auto delta = ParseFeedback(req.feedback);

        // Modify current parameters
        auto current_params = LoadParameters(req.current_effect);
        auto new_params = ApplyDelta(current_params, delta);

        // Determine what changed
        auto changes = DiffParameters(current_params, new_params);

        // Regenerate only changed components
        ShaderGenerationResult shader_result;
        Buffer texture_result;

        if (changes.HasShaderChanges()) {
            shader_result = GenerateShader(new_params);
        } else {
            shader_result = GetCachedShader(new_params);
        }

        if (changes.HasTextureChanges()) {
            texture_result = GenerateTexture(new_params);
        } else {
            texture_result = GetCachedTexture(new_params);
        }

        // Assemble and return
        auto updated_uzvfx = AssembleEffect(new_params, shader_result, texture_result, nullptr);
        return Validate(updated_uzvfx);
    }

private:
    struct ParameterDelta {
        float intensity_delta;
        Vec3 color_delta;
        float emission_rate_delta;
        // ... etc
    };

    ParameterDelta ParseFeedback(StringView feedback) {
        // Claude: interpret "More fire" → intensity +0.3, add fire keywords
        auto prompt = fmt::format(
            "User feedback: '{}' on current effect. "
            "Generate JSON with modifications needed.",
            feedback
        );
        std::string response = CallClaudeAPI(prompt);
        return ParseJSON<ParameterDelta>(response);
    }

    ParameterDelta DiffParameters(
        const VFXParameters& old,
        const VFXParameters& new
    ) {
        ParameterDelta delta;
        delta.intensity_delta = new.intensity - old.intensity;
        delta.color_delta = new.primary_color - old.primary_color;
        // ... etc
        return delta;
    }
};
```

## Visual Feedback Loop

After generating a VFX effect, the orchestrator automatically captures a **multi-frame contact sheet** (8 frames arranged in a grid) and sends it to Claude Vision for visual quality analysis. Claude evaluates:

- **Color accuracy**: Are the colors correct for the effect type? Smooth gradients?
- **Timing**: Does the effect evolve naturally over its lifetime?
- **Particle density**: Too sparse or too cluttered?
- **Silhouette & shape**: Does it match the intended effect (fire, explosion, etc.)?
- **Fade behavior**: Natural fade-out or abrupt cut?
- **Visual quality**: Artifacts, texture issues, wrong blend mode?

Claude provides a **score (0–10)** and **structured adjustments** (spawn rate %, colors, lifetime, glow, etc.). If the score is below 7, the orchestrator applies the suggested changes and **re-renders automatically**. This creates an **autonomous refinement loop** (max 3 iterations) that improves the effect without manual intervention.

**Cost per iteration**: ~$0.03 (Claude Vision input + output tokens).
**Full effect with 2 iterations**: ~$0.08–0.15 total (generation + analysis).

For detailed implementation, frame capture strategy, contact sheet composition, and iteration control, see **[Visual Feedback Loop](07-visual-feedback.md)**.

## Template System

Pre-built templates for common effects:

```cpp
/// Pre-built VFX templates (fast iteration starting points).
class VFXTemplateLibrary {
public:
    static const VFXParameters& GetTemplate(StringView name) {
        static const std::unordered_map<std::string, VFXParameters> templates = {
            { "fire", VFXParameters{
                .primary_color = {1, 0.6f, 0},
                .secondary_color = {1, 0.3f, 0},
                .intensity = 1.5f,
                .style = "fire",
                .emission_rate = 200,
                .particle_lifespan = 3.0f,
                // ...
            }},
            { "explosion", VFXParameters{
                .primary_color = {1, 0.5f, 0},
                .secondary_color = {0.8f, 0.2f, 0},
                .intensity = 2.0f,
                .style = "fire",
                .emission_rate = 500,
                .particle_lifespan = 2.0f,
                // ...
            }},
            { "electric", VFXParameters{
                .primary_color = {0, 0, 1},
                .secondary_color = {0, 1, 1},
                .intensity = 1.3f,
                .style = "electric",
                .emission_rate = 300,
                .particle_lifespan = 1.5f,
                // ...
            }},
            // ... 10+ more templates
        };
        return templates.at(name.data());
    }
};

// Usage:
auto fire_params = VFXTemplateLibrary::GetTemplate("fire");
auto result = orchestrator.Generate(fire_params);
```

## Related Documentation

- `docs/engine/33-vfx-ai-tool/01-overview.md` — Overview
- `docs/engine/33-vfx-ai-tool/02-shader-generation.md` — Shader generation
- `docs/engine/33-vfx-ai-tool/03-texture-generation.md` — Texture generation
- `docs/engine/33-vfx-ai-tool/04-mesh-generation.md` — Mesh generation
- `docs/engine/28-ipc-protocol/` — IPC communication protocol
- `docs/engine/25-vfx/` — VFX system, effect rendering
