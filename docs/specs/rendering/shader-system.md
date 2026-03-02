# Shader System Spec

## Overview

UzEngine uses **HLSL as the single shader authoring language**. Shaders are plain `.hlsl` files in the project directory — editable by humans, LLMs, and the VSCode shader editor. Hot-reload is instant via IPC.

HLSL is compiled to **SPIR-V** (canonical IR) at cook time. The RHI backend then uses the appropriate IR per platform: DXIL (DX12), SPIR-V (Vulkan), or MSL (Metal). See [RHI Spec](rhi.md) for the full cross-platform pipeline.

> **No direct GLSL authoring.** GLSL was previously listed as a future language. The correct approach is HLSL → SPIR-V → SPIRV-Cross → GLSL ES (for Android debug only). Game developers always write HLSL.

---

## Shader Types

| Stage | HLSL entry | SPIR-V execution model |
|-------|-----------|----------------------|
| Vertex | `VSMain` | `Vertex` |
| Pixel / Fragment | `PSMain` | `Fragment` |
| Compute | `CSMain` | `GLCompute` |
| Mesh (SM6.5+) | `MSMain` | `MeshNV` / `MeshEXT` |

---

## HLSL Shader Structure

```hlsl
// shaders/pbr.hlsl

// --- Constants (bound at slot b0) ---
cbuffer PerObject : register(b0) {
    float4x4 model;
    float4x4 view_proj;
};

cbuffer Material : register(b1) {
    float3 albedo;
    float  roughness;
    float  metallic;
    float  _pad[3];
};

// --- Textures ---
Texture2D    AlbedoTex  : register(t0);
Texture2D    NormalTex  : register(t1);
SamplerState Sampler    : register(s0);

// --- Vertex shader ---
struct VSIn {
    float3 pos    : POSITION;
    float3 normal : NORMAL;
    float2 uv     : TEXCOORD0;
};

struct VSOut {
    float4 pos    : SV_POSITION;
    float3 normal : NORMAL;
    float2 uv     : TEXCOORD0;
    float3 world  : TEXCOORD1;
};

VSOut VSMain(VSIn v) {
    VSOut o;
    float4 world = mul(model, float4(v.pos, 1.0));
    o.pos    = mul(view_proj, world);
    o.normal = mul((float3x3)model, v.normal);
    o.uv     = v.uv;
    o.world  = world.xyz;
    return o;
}

// --- Pixel shader (PBR GGX) ---
float4 PSMain(VSOut i) : SV_TARGET {
    float3 N  = normalize(i.normal);
    float3 V  = normalize(CameraPos - i.world);
    float3 alb = AlbedoTex.Sample(Sampler, i.uv).rgb * albedo;
    // ... GGX BRDF computation ...
    return float4(color, 1.0);
}
```

---

## Shader Compiler Pipeline

HLSL is the only authoring language. SPIR-V is the canonical IR. See [rhi.md](rhi.md) for the full cross-platform pipeline.

```
.hlsl (authored)
    ↓
DXC (DirectXShaderCompiler)
    ├── → SPIR-V (.spv)  — canonical IR, stored in .uzpak
    └── → DXIL (.bin)    — DX12 fast path (Windows only)

SPIR-V
    ├── Vulkan backend   → used as-is
    ├── Metal backend    → SPIRV-Cross → MSL
    └── Android debug    → SPIRV-Cross → GLSL ES (for tooling only)
```

### Compilation Modes

| Mode | DXC Flags | Output | Use |
|------|-----------|--------|-----|
| Debug | `-Od -Zi -spirv` | SPIR-V + PDB | Editor / development |
| Release | `-O3 -spirv` | SPIR-V (optimized) | Shipped game |
| Hot-reload | `-O1 -spirv` async | SPIR-V, swap on success | Editor loop |

> Binding rules: always use `register(bN, space0)` notation for cross-backend compatibility. See [rhi.md § HLSL Authoring Rules](rhi.md) for the full compatibility matrix.

---

## Hot-Reload Protocol

1. File watcher detects change to `.hlsl`
2. OR MCP call: `shader.reload({ "path": "shaders/pbr.hlsl" })`
3. Engine recompiles shader on background thread
4. On success: swaps PSO atomically (no frame drop)
5. On error: keeps old shader active, returns error to IPC caller

```json
// Success response
{ "ok": true, "shader": "pbr.hlsl", "stage": "VS+PS", "elapsed_ms": 42 }

// Error response
{ "ok": false, "errors": [
    { "file": "pbr.hlsl", "line": 47, "col": 12,
      "message": "error X3004: undeclared identifier 'CameraPos'" }
]}
```

---

## Material-Shader Binding

A `.uzmat` file declares which shader it uses and provides parameter values:

```ini
[uzmaterial version=1]
shader = "res://shaders/pbr.hlsl"

; Parameters match cbuffer/texture slots in the shader
albedo           = [0.8, 0.3, 0.1]
roughness        = 0.6
metallic         = 0.0
albedo_texture   = "res://textures/brick_albedo.png"
normal_texture   = "res://textures/brick_normal.png"
```

The engine validates parameter names against the shader's reflection data at load time and logs warnings for missing / mismatched uniforms.

---

## VSCode Shader Editor Features

| Feature | Implementation |
|---------|---------------|
| Syntax highlighting | HLSL language extension (VSCode marketplace) |
| Error squiggles | Engine reports errors via IPC → extension shows diagnostics |
| Hot-reload on save | File watcher in extension calls `shader.reload` IPC |
| Compilation status | Status bar item shows "Compiling..." / "OK" / error count |
| Preview frame | After reload, extension requests `engine.screenshot()` and shows thumbnail |

---

## Built-in Shaders

The engine ships with built-in shaders that cover common use cases:

| Name | File | Description |
|------|------|-------------|
| `pbr` | `shaders/builtin/pbr.hlsl` | Full PBR (GGX BRDF + IBL) |
| `pbr_no_tex` | `shaders/builtin/pbr_notex.hlsl` | PBR flat color only |
| `unlit` | `shaders/builtin/unlit.hlsl` | No lighting, just albedo |
| `wireframe` | `shaders/builtin/wireframe.hlsl` | Debug wireframe |
| `depth_only` | `shaders/builtin/depth.hlsl` | Shadow map pass |
| `skybox` | `shaders/builtin/skybox.hlsl` | Cubemap sky |
| `fullscreen` | `shaders/builtin/fullscreen.hlsl` | Post-process base |

---

## IPC Handlers (shader.*)

```json
shader.reload    { "path": "res://shaders/pbr.hlsl" }
shader.getErrors { "path": "res://shaders/pbr.hlsl" }
             → { "errors": [] }
shader.list      {}
             → { "shaders": ["pbr.hlsl", "cel.hlsl", ...] }
shader.getSource { "path": "res://shaders/pbr.hlsl" }
             → { "source": "..." }
shader.setSource { "path": "res://shaders/pbr.hlsl", "source": "..." }
             (writes file + triggers reload)
```
