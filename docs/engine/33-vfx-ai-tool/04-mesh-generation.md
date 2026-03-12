# VFX Mesh Generation — TRELLIS.2 Image-to-3D Pipeline

> **Status**: Planned (Phase 4)
> **Source**: `editor/vfx-ai-tool/mesh_generator.ts`, TRELLIS.2 REST API integration
> **References**: TRELLIS.2 (Microsoft, MIT license), mesh_simplify library, InstantMesh (alternative)

## Purpose

For mesh-based VFX effects (projectiles, debris, crystals), system generates 3D meshes from reference images using **TRELLIS.2** (Microsoft's open-source image-to-3D model). Generates complete 3D geometry with PBR textures in ~17 seconds on RTX 4090. Requires post-processing (mesh simplification, cleanup) but enables unique, AI-generated particle meshes.

**Use cases**:
- Projectile particles (magic orbs, bullets, debris)
- Environmental VFX (crystals, fragments, shards)
- Unique effect elements (complex shapes hard to model by hand)
- Rapid iteration on 3D particle designs

## Architecture

```
┌──────────────────────────────┐
│ User specifies:              │
│ - Concept description        │
│ - Or reference image         │
│ - Mesh type (crystal, gem…)  │
└────────────┬─────────────────┘
             │
             ▼
    ┌────────────────────┐
    │ If no image:       │
    │ Claude generates   │
    │ reference image    │
    │ (optional)         │
    └────────┬───────────┘
             │
             ▼
    ┌──────────────────────────┐
    │ TRELLIS.2 REST API       │
    │ (local RTX 4090 service) │
    │ Image → 3D Mesh (GLB)    │
    │ ~17 sec at 1024³         │
    └────────┬─────────────────┘
             │
             ├─ VRAM: 24 GB (RTX 4090 minimum)
             ├─ Output: GLB with PBR textures
             └─ Resolution: 1024³ voxel grid
                 │
                 ▼
    ┌──────────────────────────────┐
    │ Post-Processing              │
    │ ├── Mesh simplification      │
    │ │   (0.80–0.85 ratio)        │
    │ ├── Remove internal vertices │
    │ ├── Normalize scale/pivot    │
    │ └── Export to .uzmesh format │
    └────────┬─────────────────────┘
             │
             ▼
    ┌──────────────────────┐
    │ Asset Import & VFS   │
    │ ├── Create material  │
    │ ├── Register VFS     │
    │ └── Ready for use    │
    └──────────────────────┘
```

## Hardware Requirements

TRELLIS.2 is computationally expensive:

| GPU | VRAM | Time/Mesh | Practical |
|-----|------|-----------|-----------|
| **RTX 3090** | 24 GB | ~25–30 sec | Works, slow |
| **RTX 4090** | 24 GB | ~15–17 sec | Recommended |
| **H100** | 80 GB | ~8–10 sec | Excellent (cloud) |
| **A100** | 40 GB | ~12–15 sec | Very good (cloud) |

**Minimum: RTX 4090 (24GB VRAM)**. Smaller GPUs cannot fit the model.

**Cloud alternatives** (if user lacks local GPU):
- **RunPod** (NVIDIA H100): $0.40–0.60/hr
- **Paperspace** (H100): $0.50–1.00/hr
- **Lambda Labs** (A100): $0.40–0.80/hr

System can optionally delegate to cloud if local unavailable.

## TRELLIS.2 Overview

### What It Is

TRELLIS.2 is a 4-billion-parameter diffusion model that generates 3D geometry from single images. Input: a 2D image. Output: a 3D mesh (GLB) with diffuse + normal + roughness textures.

**Paper**: "TRELLIS: Transformer Encoder-based Latent Image Sampling" (Microsoft)
**License**: MIT (open-source)
**Model size**: ~8 GB
**Input**: 512×512 PNG image
**Output**: GLB file with PBR textures

### Quality & Limitations

**Quality**: Good for stylized effects. Photogrammetry-quality on good reference images. May hallucinate details on poor/ambiguous images.

**Limitations**:
- Assumes single object (not scenes)
- May misinterpret very abstract or noisy images
- Generates lower poly than hand-modeled (needs simplification)
- Not real-time (17 sec per mesh)

### Practical Tips

**Best results with**:
- Clear, well-lit reference images
- Unambiguous subject matter
- Concept art or AI-generated images (Claude → SDXL → TRELLIS.2 pipeline)

**Worst results with**:
- Blurry, low-contrast images
- Complex scenes with multiple objects
- Abstract or symbolic imagery

## Mesh Generation Pipeline

### Step 1: Reference Image Acquisition

User provides reference image, OR system generates one:

```typescript
// Option A: User provides image
const referenceImage = await selectImageFromDisk();

// Option B: Claude generates concept + SDXL generates image
const conceptPrompt = "red crystalline gem, faceted, glowing";
const conceptImage = await claudeImageGenerator.Generate(conceptPrompt);
const refImage = await sdxlTextureGenerator.Generate(conceptImage);

// Option C: Template reference (pre-baked library)
const templateImage = VFXMeshTemplates.GetCrystal();
```

### Step 2: TRELLIS.2 Generation

System calls TRELLIS.2 REST API:

```cpp
/// Generate 3D mesh from reference image via TRELLIS.2.
class TRELLIS2MeshGenerator {
public:
    struct GenerateResult {
        bool success;
        std::vector<uint8_t> glb_bytes;  // Binary GLB file
        std::string error_message;
        float generation_time_sec;
    };

    /// Generate mesh from reference image.
    /// Blocks until generation complete (~17 sec on RTX 4090).
    GenerateResult GenerateMesh(
        const std::vector<uint8_t>& reference_image_png,
        int voxel_resolution = 1024,  // 512, 1024, or 2048
        int num_steps = 64              // diffusion steps (64–128)
    );

private:
    std::string service_url_;  // e.g., "http://localhost:8889"

    /// POST request to TRELLIS.2 service
    std::string CallTRELLIS2(const std::vector<uint8_t>& image_png);

    /// Poll service for result
    std::vector<uint8_t> PollResult(StringView job_id);
};

// Usage:
TRELLIS2MeshGenerator gen("http://localhost:8889");
auto image_bytes = FileSystem::ReadBinary("concept_image.png");
auto result = gen.GenerateMesh(image_bytes, 1024, 64);

if (result.success) {
    FileSystem::WriteBinary("gem_raw.glb", result.glb_bytes);
    UZ_LOG(VFX, Info, "Generated mesh in {:.1f}s", result.generation_time_sec);
} else {
    UZ_LOG(VFX, Error, "TRELLIS.2 failed: {}", result.error_message);
}
```

### Step 3: Mesh Post-Processing

Raw TRELLIS.2 output needs cleanup:

```cpp
/// Post-process TRELLIS.2 mesh for VFX use.
class VFXMeshProcessor {
public:
    struct ProcessResult {
        Mesh simplified_mesh;      // Reduced poly count
        std::vector<Material> mats; // PBR materials
    };

    /// Simplify + cleanup + normalize mesh.
    ProcessResult ProcessMesh(
        const Mesh& raw_mesh,
        float simplification_ratio = 0.8f,  // Keep 80% of verts
        bool remove_internals = true,       // Remove non-visible faces
        bool normalize_scale = true         // Fit to 1x1x1 box
    );

private:
    /// Mesh simplification via mesh_simplify library.
    /// Preserves silhouette, reduces poly count.
    Mesh Simplify(const Mesh& mesh, float target_ratio);

    /// Remove internal faces (hidden by other faces).
    /// Keeps only surfaces visible from outside.
    Mesh RemoveInternals(const Mesh& mesh);

    /// Normalize mesh to 0–1 bounding box, center at origin.
    Mesh NormalizeScale(const Mesh& mesh);

    /// Recalculate per-vertex normals and tangents.
    void RecalculateNormals(Mesh& mesh);
};

// Usage in pipeline:
VFXMeshProcessor processor;
auto processed = processor.ProcessMesh(raw_trellis_mesh, 0.8f);

// Result: 50–80% poly reduction, cleaned up, ready for GPU
```

**Example simplification**:
- Raw TRELLIS.2: ~500k triangles, complex internal geometry
- After simplification (0.80 ratio): ~100k triangles
- Suitable for instanced particle rendering (1000+ instances at 60 FPS)

### Step 4: Import as .uzmesh Asset

Processed mesh imported into project:

```cpp
/// Import post-processed mesh as .uzmesh asset.
void ImportVFXMesh(
    StringView glb_path,
    StringView output_uzmap_path
) {
    Mesh mesh = LoadGLB(glb_path);
    auto processor = VFXMeshProcessor();
    auto processed = processor.ProcessMesh(mesh, 0.8f);

    // Create material for each submesh
    std::vector<Material> materials;
    for (const auto& mat : processed.materials) {
        Material uz_mat;
        uz_mat.diffuse = ImportTexture(mat.diffuse_texture);
        uz_mat.normal = ImportTexture(mat.normal_texture);
        uz_mat.roughness = ImportTexture(mat.roughness_texture);
        materials.push_back(uz_mat);
    }

    // Save as .uzmesh
    UZMesh uz_mesh(processed.mesh, materials);
    uz_mesh.Save(output_uzmesh_path);
}
```

## Mesh-Based VFX Usage

Once imported, mesh used in VFX emitters:

```json
{
    "format": "uzvfx",
    "name": "crystal_burst",
    "type": "emitter",

    "spawn": {
        "rate": 50,
        "lifespan_min": 2.0,
        "lifespan_max": 4.0,
        "velocity": [0, 0, 8],
        "velocity_variation": 10.0
    },

    "render": {
        "type": "mesh",  // NOT sprite — mesh particles
        "mesh": "generated/crystal_shard.uzmesh",
        "material": "generated/crystal_material.uzmaterial",
        "instance_count": 1000,  // 1000 instances per emitter
        "sort": "back_to_front"
    }
}
```

Runtime rendering:

```cpp
/// Mesh particle emitter (instanced rendering).
class MeshParticleEmitter : public ParticleEmitter {
public:
    ResourceHandle<Mesh> mesh;
    uint32_t max_instances = 10000;

    /// Render via GPU instancing (one draw call, many instances).
    void Render(RHI::CommandList& cmd) override {
        // Instance buffer contains per-particle transforms
        cmd.DrawInstancedMesh(mesh, max_instances, instance_buffer);
    }
};
```

## Alternatives & Comparisons

| System | Input | Output | VRAM | Time | Open | Use? |
|--------|-------|--------|------|------|------|------|
| **TRELLIS.2** | Image | GLB + PBR | 24 GB | 17 sec | ✅ MIT | Primary |
| **InstantMesh** | Image | OBJ/PLY | 8 GB | 5 sec | ✅ | Phase 4 (if faster) |
| **Hunyuan 3D** | Image | GLB | 20 GB | 10 sec | ⏳ When available | Phase 4+ |
| **Google Tripo** | Image | GLB | ? | ? | ❌ Proprietary | No |
| **OpenLRM** | Image | .obj | 4 GB | 8 sec | ✅ | Phase 4 (lightweight) |
| **Hand-modeling** | N/A | GLB | N/A | 1–2 hours | ✅ | Backup |

**Recommendation**: Start with TRELLIS.2 (quality + reasonable requirements). Phase 4, add InstantMesh (faster) or OpenLRM (lighter) as alternatives.

## Cloud Deployment Options

If user lacks RTX 4090, can deploy to cloud:

### RunPod (Recommended)

```bash
# Rent H100 for TRELLIS.2
# Cost: $0.44/hr (H100 + A100 mix)
# Step 1: Launch pod with TRELLIS.2 container
# Step 2: Engine calls https://<pod-id>.runpod.io/api/generate
# Step 3: Mesh returned in ~8–10 seconds
```

### Paperspace

```bash
# Rent A100 GPU
# Cost: $0.51/hr
# Similar HTTP API workflow
```

### System Integration

```cpp
/// Optionally use cloud service if local unavailable.
class MeshGenerationService {
private:
    enum Backend { Local, RunPod, Paperspace };
    Backend backend_;

public:
    MeshGenerationService() {
        if (IsLocalServiceRunning("http://localhost:8889")) {
            backend_ = Backend::Local;
        } else if (HasCloudCredentials()) {
            backend_ = Backend::RunPod;  // or Paperspace
        } else {
            UZ_LOG(VFX, Warning, "No mesh generation available (need RTX 4090 or cloud)");
        }
    }

    GenerateResult Generate(const std::vector<uint8_t>& image) {
        switch (backend_) {
            case Backend::Local:
                return GenerateLocal(image);
            case Backend::RunPod:
                return GenerateRunPod(image);
            case Backend::Paperspace:
                return GeneratePaperspace(image);
        }
    }
};
```

## Fallback: Primitive Meshes

If TRELLIS.2 unavailable, system falls back to engine-provided primitives:

```cpp
/// VFX mesh library (built-in fallback).
class VFXMeshLibrary {
public:
    static Mesh GetSphere();
    static Mesh GetCube();
    static Mesh GetCone();
    static Mesh GetOctahedron();
    static Mesh GetTetrahedron();

    /// Generate UV-mapped sphere (good for many effects).
    static Mesh GetParametricSphere(int subdivisions = 2);
};

// Usage: if TRELLIS.2 fails
Mesh mesh = (trellis_generation_succeeded)
    ? processed_trellis_mesh
    : VFXMeshLibrary::GetParametricSphere(3);
```

**Result**: Effect still renders, just uses generic geometry instead of unique AI-generated shape.

## Example: Full Mesh VFX Generation

End-to-end pipeline:

```
User: "Generate a red crystal explosion with debris"
  │
  ├─ Claude generates concept image description
  │  "High-poly faceted red crystal gem, sharp edges, glossy"
  │
  ├─ SDXL generates reference image from description
  │  (512×512 PNG of shiny red crystal)
  │
  ├─ TRELLIS.2 generates 3D mesh from reference image
  │  (500k triangles, with PBR textures)
  │
  ├─ Mesh simplification reduces to ~100k triangles
  │  (removed internals, optimized for instancing)
  │
  ├─ Claude generates shader for crystal material
  │  (normal-mapped, glossy, refractive)
  │
  ├─ Claude generates .uzvfx definition
  │  (100 particles/sec, 2–3 sec lifespan, 50m spread)
  │
  └─ Preview renders in viewport, user iterates
     "More particles", "Slower fall", "More red"
     → System regenerates only changed components
```

**Total time**: ~30–40 seconds (Claude: 3s, SDXL: 4s, TRELLIS.2: 17s, post-process: 2s, preview: 5s)
**Total cost**: ~$0.05–0.10 (API calls only, local GPU cost minimal)

## Related Documentation

- `docs/engine/33-vfx-ai-tool/01-overview.md` — Overview
- `docs/engine/33-vfx-ai-tool/02-shader-generation.md` — Claude shaders
- `docs/engine/33-vfx-ai-tool/03-texture-generation.md` — Texture generation
- `docs/engine/25-vfx/` — VFX system, particle rendering
- `docs/engine/08-resource-system/` — Asset formats, `.uzmesh`
