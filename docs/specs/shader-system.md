# Shader System Spec

## Overview

UzEngine supports **HLSL** (primary, DX12) and **GLSL** (future, Vulkan). Shaders are plain `.hlsl` / `.glsl` files in the project directory — editable by humans, LLMs, and the VSCode shader editor. Hot-reload is instant via IPC.

---

## Shader Types

| Stage | HLSL entry | GLSL stage |
|-------|-----------|-----------|
| Vertex | `VSMain` | `main` (vert) |
| Pixel / Fragment | `PSMain` | `main` (frag) |
| Compute | `CSMain` | `main` (comp) |

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

## GLSL Shader Structure (Future)

```glsl
// shaders/pbr.glsl
#version 450

// Vertex
layout(location=0) in vec3 in_pos;
layout(location=1) in vec3 in_normal;
layout(location=2) in vec2 in_uv;

layout(set=0, binding=0) uniform PerObject {
    mat4 model;
    mat4 view_proj;
};

layout(location=0) out vec3 frag_normal;
layout(location=1) out vec2 frag_uv;

void main() {
    gl_Position = view_proj * model * vec4(in_pos, 1.0);
    frag_normal = mat3(model) * in_normal;
    frag_uv     = in_uv;
}
```

---

## Shader Compiler Pipeline

```
.hlsl file
    ↓
DXC (DirectXShaderCompiler)
    ├── Debug build  → DXIL + PDB (shader debugging)
    └── Release build → DXIL (stripped)
            ↓
    D3D12 PSO (Pipeline State Object)
            ↓
    Bound to draw calls

.glsl file (future)
    ↓
glslang
    ↓
SPIR-V
    ↓
Vulkan VkShaderModule
```

### Compilation Modes

| Mode | Flags | Use |
|------|-------|-----|
| Debug | `-Od -Zi` | Editor / development — step debugging |
| Release | `-O3` | Shipped game |
| Hot-reload | Async recompile, swap on success | Editor loop |

---

## Hot-Reload Protocol

1. File watcher detects change to `.hlsl` / `.glsl`
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
| GLSL support | Built-in VSCode GLSL extension |

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
