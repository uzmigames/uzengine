# RHI Configuration Resolution

Configuration for the rendering backend, shader model, and debug features.

> **Header**: `runtime/core/rhi/rhi_config.h` |
> **API**: [ResolveRHIConfig()](#resolveRHIConfig) | [RHIConfig struct](#RHIConfig) | [ShaderModel enum](#ShaderModel)

## Overview

The RHI configuration determines which GPU API and shader model are used at runtime. Configuration is resolved from multiple sources with cascading priority:

```
CLI args (highest priority)
    ↓
.uzproject config
    ↓
Platform auto-detect (lowest priority)
```

---

## Resolution Priority

### 1. CLI Arguments (Highest Priority)

Command-line flags override everything:

```bash
./uzengine \
    --rhi dx12 \
    --shader-model sm6.6 \
    --gpu-validation \
    --gpu-breadcrumbs \
    --adapter 0
```

### 2. .uzproject Config

If CLI args aren't provided, the engine reads `.uzproject`:

```json
{
  "project": {
    "name": "MyGame"
  },
  "render": {
    "rhi": "dx12",
    "shader_model": "sm6.6",
    "gpu_validation": false,
    "gpu_breadcrumbs": false,
    "adapter_index": 0
  }
}
```

### 3. Platform Auto-Detect

If neither CLI nor config specify a backend:

| Platform | Default Backend | Default Shader Model |
|----------|-----------------|----------------------|
| Windows | DX12 | SM6.6 (or negotiated lower) |
| Linux | Vulkan | SM5.0 (fallback) |
| macOS | Metal | SM5.0 (fallback) |

---

## RHIConfig Struct

The resolved configuration ready for device creation.

```cpp
struct RHIConfig {
    RHIBackend backend = RHIBackend::DX12;
    ShaderModel shaderModel = ShaderModel::SM6_6;
    bool enableValidation = false;
    bool enableGpuValidation = false;
    bool enableBreadcrumbs = false;
    uint32_t adapterIndex = 0;
};
```

### Fields

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `backend` | `RHIBackend` | `DX12` | GPU API to use (DX12, Vulkan, Metal, Null) |
| `shaderModel` | `ShaderModel` | `SM6_6` | Shader model level (SM5.0–SM6.7) |
| `enableValidation` | `bool` | `false` | D3D12 debug layer + validation (DX12 only) |
| `enableGpuValidation` | `bool` | `false` | GPU-based validation (slower but thorough) |
| `enableBreadcrumbs` | `bool` | `false` | GPU breadcrumbs for TDR diagnostics |
| `adapterIndex` | `uint32_t` | `0` | GPU adapter index (0 = first adapter) |

---

## ShaderModel Enum

| Enum | Value | Description |
|------|-------|-------------|
| `SM5_0` | 50 | DirectX 11 baseline |
| `SM5_1` | 51 | DirectX 11.3+ (dynamic indexing) |
| `SM6_0` | 60 | DirectX 12 baseline (wave intrinsics) |
| `SM6_1` | 61 | Barycentric intrinsics, SV_ViewID |
| `SM6_2` | 62 | Float16, denorm mode control |
| `SM6_3` | 63 | DirectX Raytracing (DXR 1.0) |
| `SM6_4` | 64 | Variable rate shading, low-precision packed dot |
| `SM6_5` | 65 | DXR 1.1, mesh shaders, sampler feedback |
| `SM6_6` | 66 | DirectX 12 Ultimate (bindless, dynamic resources) |
| `SM6_7` | 67 | Advanced texture ops, relaxed precision |

### Hardware Support

| GPU | Max Supported SM | Feature Level |
|-----|------------------|----------------|
| RTX 4090 (Ada) | SM6.7 | FL 12.2 (with Agility SDK) |
| RTX 4080 (Ada) | SM6.7 | FL 12.2 (with Agility SDK) |
| RTX 3090 (Ampere) | SM6.6 | FL 12.2 (with Agility SDK) |
| RTX 2070 (Turing) | SM6.5 | FL 12.1 |
| GTX 1080 (Pascal) | SM6.3 | FL 12_0 |
| Older (Maxwell, Kepler) | SM5.1 | FL 11_1 |

---

## RHIBackend Enum

| Backend | Status | Platform | Notes |
|---------|--------|----------|-------|
| `DX12` | ✅ Implemented | Windows 10+ | Primary backend, SM5–SM6.7 support |
| `Vulkan` | 📋 Planned | Windows, Linux | Cross-platform fallback |
| `Metal` | 📋 Planned | macOS | Apple Silicon & Intel Macs |
| `Null` | ✅ Implemented | All | Headless rendering (testing) |

---

## ResolveRHIConfig()

Resolves configuration from all sources and returns a ready-to-use `RHIConfig`.

### Signature

```cpp
[[nodiscard]] RHIConfig ResolveRHIConfig(const Config& config);
```

### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `config` | `const Config&` | Engine config (loads .uzproject and CLI args) |

### Returns

`RHIConfig` with resolved backend, shader model, and debug flags.

### Example

```cpp
#include "core/config/config.h"
#include "core/rhi/rhi_config.h"

// Load config from .uzproject + CLI args
Config config;
config.LoadFromFile("project.uzproject");
config.ApplyCLIOverrides(argc, argv);

// Resolve RHI configuration
RHIConfig rhiConfig = ResolveRHIConfig(config);

// Create device with resolved settings
RHIDeviceDesc deviceDesc = ToDeviceDesc(rhiConfig);
auto* device = CreateRHIDevice(deviceDesc);
```

---

## CLI Flag Formats

### --rhi

Specify the GPU backend.

```bash
--rhi dx12          # DirectX 12 (Windows only)
--rhi vulkan        # Vulkan (Windows, Linux, Android)
--rhi metal         # Metal (macOS only)
--rhi null          # Headless (no GPU output)
```

### --shader-model

Specify the shader model level.

```bash
--shader-model sm5      # SM5.0 (DirectX 11)
--shader-model sm5.1    # SM5.1
--shader-model sm6      # SM6.0
--shader-model sm6.1    # SM6.1
--shader-model sm6.2    # SM6.2
--shader-model sm6.3    # SM6.3
--shader-model sm6.4    # SM6.4
--shader-model sm6.5    # SM6.5
--shader-model sm6.6    # SM6.6 (default)
--shader-model sm6.7    # SM6.7
```

### --gpu-validation

Enable GPU-based validation (DX12 only, slow).

```bash
--gpu-validation        # Enable GPU-based checks
```

This is more thorough than CPU-side validation but has significant performance cost. Use only for debugging.

### --gpu-breadcrumbs

Enable GPU breadcrumbs for TDR diagnostics.

```bash
--gpu-breadcrumbs       # Track last GPU commands before device removal
```

On GPU timeout (TDR), breadcrumb markers show which command(s) caused the hang.

### --adapter

Specify which GPU adapter to use (for multi-GPU systems).

```bash
--adapter 0             # First GPU (default)
--adapter 1             # Second GPU
--adapter 2             # Third GPU
```

Use `enumerate-adapters` to list available GPUs:

```bash
./uzengine --enumerate-adapters
# Output:
# Adapter 0: NVIDIA RTX 4090 (VRAM: 24GB)
# Adapter 1: NVIDIA RTX 3090 (VRAM: 24GB)
```

---

## Shader Model Negotiation (DX12)

The DX12 backend automatically selects the highest supported shader model:

1. **Check hardware capability** via `D3D12_FEATURE_SHADER_MODEL` query
2. **Check Resource Binding Tier** (Tier 3 required for SM6.6+ bindless)
3. **Select highest supported:**
   - SM6.7 (if available)
   - SM6.6 (if Tier 3 bindless available)
   - SM6.0 (standard DX12)
   - SM5.1 (fallback)
   - SM5.0 (last resort)
4. **Adapt root signature:**
   - SM6.6+ uses `DIRECTLY_INDEXED` descriptor heaps (bindless)
   - SM5.x uses traditional root signatures with descriptor tables

Example on RTX 4090 with Agility SDK:

```
Requested: SM6.6
Hardware supports: SM6.7 (with Agility SDK)
Selected: SM6.6 (conservative, stable)
Root signature: DIRECTLY_INDEXED (bindless)
Feature level: 12_2 (with Agility SDK)
```

You can override the automatic selection via `--shader-model sm6.0` to force a lower model for testing fallback paths.

---

## Conversion Utilities

### RHIBackendToString()

Convert backend enum to human-readable string.

```cpp
const char* str = RHIBackendToString(RHIBackend::DX12);  // "DX12"
```

### ShaderModelToString()

Convert shader model enum to human-readable string.

```cpp
const char* str = ShaderModelToString(ShaderModel::SM6_6);  // "SM6.6"
```

### ToDeviceDesc()

Convert `RHIConfig` to `RHIDeviceDesc` for device creation.

```cpp
RHIConfig config = ResolveRHIConfig(cfg);
RHIDeviceDesc deviceDesc = ToDeviceDesc(config);
auto* device = CreateRHIDevice(deviceDesc);
```

---

## .uzproject Schema

Configuration in the project file under `render` key:

```json
{
  "project": {
    "name": "MyGame",
    "version": "1.0.0"
  },
  "render": {
    "rhi": "dx12",
    "shader_model": "sm6.6",
    "gpu_validation": false,
    "gpu_breadcrumbs": false,
    "adapter_index": 0
  }
}
```

Valid values:

| Key | Values |
|-----|--------|
| `rhi` | `"dx12"`, `"vulkan"`, `"metal"`, `"null"` |
| `shader_model` | `"sm5"`, `"sm5.1"`, `"sm6"`, `"sm6.1"`, `"sm6.2"`, `"sm6.3"`, `"sm6.4"`, `"sm6.5"`, `"sm6.6"`, `"sm6.7"` |
| `gpu_validation` | `true` or `false` |
| `gpu_breadcrumbs` | `true` or `false` |
| `adapter_index` | Non-negative integer |

---

## Complete Example

```cpp
#include "core/config/config.h"
#include "core/rhi/rhi_config.h"
#include "core/rhi/rhi.h"

int main(int argc, char* argv[]) {
    // Load configuration
    Config config;
    config.LoadFromFile("project.uzproject");
    config.ApplyCLIOverrides(argc, argv);

    // Resolve RHI config (CLI > .uzproject > auto-detect)
    RHIConfig rhiConfig = ResolveRHIConfig(config);

    // Log resolved settings
    UZ_LOG(Core, "RHI Backend: {}", RHIBackendToString(rhiConfig.backend));
    UZ_LOG(Core, "Shader Model: {}", ShaderModelToString(rhiConfig.shaderModel));
    UZ_LOG(Core, "GPU Validation: {}", rhiConfig.enableGpuValidation);
    UZ_LOG(Core, "GPU Breadcrumbs: {}", rhiConfig.enableBreadcrumbs);

    // Create device
    RHIDeviceDesc deviceDesc = ToDeviceDesc(rhiConfig);
    auto* device = CreateRHIDevice(deviceDesc);

    if (!device) {
        UZ_LOG_ERROR(Core, "Failed to create RHI device");
        return 1;
    }

    // ... use device ...

    DestroyRHIDevice(device);
    return 0;
}
```

---

## Related Pages

- [RHI Overview](../../guides/rhi/overview.md) — High-level design
- [Getting Started](../../getting-started/first-window.md) — Practical walkthrough
- [Architecture](../../guides/architecture.md) — System design
