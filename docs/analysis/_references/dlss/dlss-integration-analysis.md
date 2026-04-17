<!-- doc-meta
title: DLSS Integration Analysis — Root Cause of 0xBAD00005
category: analysis
created: 2026-04-06
status: active
-->

# DLSS Integration Analysis: Root Cause of 0xBAD00005 (InvalidParameter)

## Executive Summary

UzEngine's DLSS integration correctly loads `_nvngx.dll`, initializes NGX, and creates
the DLSS feature. However, `EvaluateFeature` always returns `0xBAD00005`
(`NVSDK_NGX_Result_FAIL_InvalidParameter`). After analyzing the official NVIDIA DLSS
SDK headers, Falcor's working implementation, and the UzEngine code, there are
**three root causes** contributing to this failure.

## Root Causes

### Root Cause 1 (PRIMARY): C++ Virtual Method Dispatch ABI Mismatch

**The Problem**: UzEngine uses `p->Set(NVSDK_NGX_Parameter_Color, colorRes)` to set
`ID3D12Resource*` parameters via the C++ virtual method table of `NVSDK_NGX_Parameter`.
The vtable was created by NVIDIA's MSVC-compiled `_nvngx.dll`, but UzEngine is compiled
with Zig/mingw. While both compilers use COM-compatible single-inheritance vtable layout,
the overload resolution happens at **UzEngine's compile time**, not at the DLL's.

The `NVSDK_NGX_Parameter` struct has 8 `Set` overloads in this order (from `nvsdk_ngx_params.h`):

```
vtable[0]: Set(const char*, unsigned long long)
vtable[1]: Set(const char*, float)
vtable[2]: Set(const char*, double)
vtable[3]: Set(const char*, unsigned int)
vtable[4]: Set(const char*, int)
vtable[5]: Set(const char*, ID3D11Resource*)
vtable[6]: Set(const char*, ID3D12Resource*)  <-- we want this
vtable[7]: Set(const char*, void*)
```

If Zig/mingw resolves `ID3D12Resource*` to `void*` (because the forward declaration
`typedef struct ID3D12Resource ID3D12Resource;` may not be sufficient to distinguish
from `void*` in the overload set), the call dispatches to **vtable[7]** (`Set(void*)`
instead of **vtable[6]** (`Set(ID3D12Resource*)`). The NGX runtime then fails to find
the resource in the correct parameter slot, returning `InvalidParameter`.

**The Fix**: Use the C function wrappers (`NVSDK_NGX_Parameter_SetD3d12Resource`,
`NVSDK_NGX_Parameter_SetF`, `NVSDK_NGX_Parameter_SetUI`, `NVSDK_NGX_Parameter_SetI`)
instead of virtual method calls. These are exported from `nvsdk_ngx_d.lib` and resolve
the overload on NVIDIA's side. Since UzEngine cannot link `nvsdk_ngx_d.lib` (MSVC CRT
incompatibility with Zig), **dynamically load these C functions from `_nvngx.dll`**.

Checking whether `_nvngx.dll` exports the `NVSDK_NGX_Parameter_Set*` symbols: the
static lib `nvsdk_ngx_d.lib` forwards calls to `_nvngx.dll`, so these symbols should
be available via `GetProcAddress`. If not, the alternative is to use the static lib's
symbols by loading `nvsdk_ngx_d.dll` (the redistrib DLL shipped with the DLSS SDK) which
does export them.

If neither approach works, the **failsafe fix** is to ensure the vtable slot is correct
by explicitly casting:

```cpp
// Force the correct overload via explicit cast to the ID3D12Resource* Set slot
using SetD3D12Fn = void(NVSDK_NGX_Parameter::*)(const char*, ID3D12Resource*);
auto setD3D12 = static_cast<SetD3D12Fn>(&NVSDK_NGX_Parameter::Set);
(p->*setD3D12)(NVSDK_NGX_Parameter_Color, colorRes);
```

### Root Cause 2 (LIKELY CONTRIBUTING): Wrong EvaluateFeature Export

**The Problem**: UzEngine loads `NVSDK_NGX_D3D12_EvaluateFeature` via `GetProcAddress`:

```cpp
// UzEngine (dlss_upscaler.cpp line 191)
pfnEvaluateFeature = reinterpret_cast<PFN_NVSDK_NGX_D3D12_EvaluateFeature>(
    ::GetProcAddress(ngxModule, "NVSDK_NGX_D3D12_EvaluateFeature"));
```

But **ALL** NVIDIA helper functions (`NGX_D3D12_EVALUATE_DLSS_EXT` and every variant)
call `NVSDK_NGX_D3D12_EvaluateFeature_C` (the C-linkage version), not the C++ version.
Falcor also calls through `NGX_D3D12_EVALUATE_DLSS_EXT` which internally calls `_C`.

The C++ version `NVSDK_NGX_D3D12_EvaluateFeature` takes `PFN_NVSDK_NGX_ProgressCallback`
(using `bool&`), while the `_C` version takes `PFN_NVSDK_NGX_ProgressCallback_C`
(using `bool*`). When compiled with Zig/mingw, the C++ name-mangled export may not
resolve correctly via `GetProcAddress` with the plain C name.

**The Fix**: Load `NVSDK_NGX_D3D12_EvaluateFeature_C` instead:

```cpp
pfnEvaluateFeature = reinterpret_cast<PFN_NVSDK_NGX_D3D12_EvaluateFeature>(
    ::GetProcAddress(ngxModule, "NVSDK_NGX_D3D12_EvaluateFeature_C"));
```

Note: The `_C` variant takes `PFN_NVSDK_NGX_ProgressCallback_C` (with `bool*`), so
the typedef should be updated:

```cpp
using PFN_NVSDK_NGX_D3D12_EvaluateFeature = NVSDK_NGX_Result(NVSDK_CONV*)(
    ID3D12GraphicsCommandList*, const NVSDK_NGX_Handle*,
    const NVSDK_NGX_Parameter*, PFN_NVSDK_NGX_ProgressCallback_C);
```

### Root Cause 3 (POSSIBLE): Missing AutoExposure Flag Consistency

**The Problem**: UzEngine creates the DLSS feature with
`NVSDK_NGX_DLSS_Feature_Flags_AutoExposure`, but Falcor does NOT use this flag.
When `AutoExposure` is set, DLSS expects the engine to NOT provide an exposure texture
and will compute exposure internally. However, UzEngine also sets:

```cpp
p->Set("DLSS.Pre.Exposure", 1.0f);
p->Set("DLSS.Exposure.Scale", 1.0f);
```

Setting exposure-related parameters while also requesting AutoExposure may cause a
parameter validation conflict inside `nvngx_dlss.dll`.

**The Fix**: Either:
- Remove `NVSDK_NGX_DLSS_Feature_Flags_AutoExposure` from creation flags (and provide
  a 1x1 R32F exposure texture with value 1.0), OR
- Keep `AutoExposure` but remove the manual exposure parameter sets

Falcor's approach: no `AutoExposure` flag, provides `pInExposureTexture` (nullable).

## Complete Correct Parameter List for EvaluateFeature

Based on `nvsdk_ngx_helpers.h` line 480-543 (`NGX_D3D12_EVALUATE_DLSS_EXT`):

### Required Parameters (MUST be set)

| Parameter Name | Type | Description |
|---|---|---|
| `NVSDK_NGX_Parameter_Color` | `ID3D12Resource*` | Scene color input (SRV state) |
| `NVSDK_NGX_Parameter_Output` | `ID3D12Resource*` | Upscaled output (UAV state) |
| `NVSDK_NGX_Parameter_Depth` | `ID3D12Resource*` | Depth buffer (SRV state) |
| `NVSDK_NGX_Parameter_MotionVectors` | `ID3D12Resource*` | Motion vectors (SRV state) |
| `NVSDK_NGX_Parameter_Jitter_Offset_X` | `float` | Jitter X in pixels |
| `NVSDK_NGX_Parameter_Jitter_Offset_Y` | `float` | Jitter Y in pixels |
| `NVSDK_NGX_Parameter_Sharpness` | `float` | Sharpness [0, 1] |
| `NVSDK_NGX_Parameter_Reset` | `int` | 1 to reset accumulation |
| `NVSDK_NGX_Parameter_MV_Scale_X` | `float` | MV scale X (default 1.0) |
| `NVSDK_NGX_Parameter_MV_Scale_Y` | `float` | MV scale Y (default 1.0) |
| `NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Width` | `unsigned int` | Render width |
| `NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Height` | `unsigned int` | Render height |
| `NVSDK_NGX_Parameter_DLSS_Pre_Exposure` | `float` | Pre-exposure (1.0 if unused) |
| `NVSDK_NGX_Parameter_DLSS_Exposure_Scale` | `float` | Exposure scale (1.0 if unused) |

### Subrect Base Parameters (required, all 0 for fullscreen)

| Parameter Name | Type |
|---|---|
| `NVSDK_NGX_Parameter_DLSS_Input_Color_Subrect_Base_X` | `unsigned int` |
| `NVSDK_NGX_Parameter_DLSS_Input_Color_Subrect_Base_Y` | `unsigned int` |
| `NVSDK_NGX_Parameter_DLSS_Input_Depth_Subrect_Base_X` | `unsigned int` |
| `NVSDK_NGX_Parameter_DLSS_Input_Depth_Subrect_Base_Y` | `unsigned int` |
| `NVSDK_NGX_Parameter_DLSS_Input_MV_SubrectBase_X` | `unsigned int` |
| `NVSDK_NGX_Parameter_DLSS_Input_MV_SubrectBase_Y` | `unsigned int` |
| `NVSDK_NGX_Parameter_DLSS_Input_Translucency_SubrectBase_X` | `unsigned int` |
| `NVSDK_NGX_Parameter_DLSS_Input_Translucency_SubrectBase_Y` | `unsigned int` |
| `NVSDK_NGX_Parameter_DLSS_Input_Bias_Current_Color_SubrectBase_X` | `unsigned int` |
| `NVSDK_NGX_Parameter_DLSS_Input_Bias_Current_Color_SubrectBase_Y` | `unsigned int` |
| `NVSDK_NGX_Parameter_DLSS_Output_Subrect_Base_X` | `unsigned int` |
| `NVSDK_NGX_Parameter_DLSS_Output_Subrect_Base_Y` | `unsigned int` |

### Optional Parameters (nullable resources, set to nullptr/0)

| Parameter Name | Type |
|---|---|
| `NVSDK_NGX_Parameter_TransparencyMask` | `ID3D12Resource*` |
| `NVSDK_NGX_Parameter_ExposureTexture` | `ID3D12Resource*` |
| `NVSDK_NGX_Parameter_DLSS_Input_Bias_Current_Color_Mask` | `ID3D12Resource*` |
| `NVSDK_NGX_Parameter_GBuffer_Albedo` through `_15` | `ID3D12Resource*` |
| `NVSDK_NGX_Parameter_TonemapperType` | `unsigned int` |
| `NVSDK_NGX_Parameter_MotionVectors3D` | `ID3D12Resource*` |
| `NVSDK_NGX_Parameter_IsParticleMask` | `ID3D12Resource*` |
| `NVSDK_NGX_Parameter_AnimatedTextureMask` | `ID3D12Resource*` |
| `NVSDK_NGX_Parameter_DepthHighRes` | `ID3D12Resource*` |
| `NVSDK_NGX_Parameter_Position_ViewSpace` | `ID3D12Resource*` |
| `NVSDK_NGX_Parameter_FrameTimeDeltaInMsec` | `float` |
| `NVSDK_NGX_Parameter_RayTracingHitDistance` | `ID3D12Resource*` |
| `NVSDK_NGX_Parameter_MotionVectorsReflection` | `ID3D12Resource*` |
| `NVSDK_NGX_Parameter_DLSS_Indicator_Invert_X_Axis` | `int` |
| `NVSDK_NGX_Parameter_DLSS_Indicator_Invert_Y_Axis` | `int` |

### UzEngine's Missing Parameters

UzEngine's current code is missing these parameters that the official helper sets:

1. `NVSDK_NGX_Parameter_TransparencyMask` (nullptr)
2. `NVSDK_NGX_Parameter_ExposureTexture` (nullptr)
3. `NVSDK_NGX_Parameter_DLSS_Input_Bias_Current_Color_Mask` (nullptr)
4. ALL 16 `NVSDK_NGX_Parameter_GBuffer_*` entries (nullptr)
5. `NVSDK_NGX_Parameter_TonemapperType` (0)
6. `NVSDK_NGX_Parameter_MotionVectors3D` (nullptr)
7. `NVSDK_NGX_Parameter_IsParticleMask` (nullptr)
8. `NVSDK_NGX_Parameter_AnimatedTextureMask` (nullptr)
9. `NVSDK_NGX_Parameter_DepthHighRes` (nullptr)
10. `NVSDK_NGX_Parameter_Position_ViewSpace` (nullptr)
11. `NVSDK_NGX_Parameter_FrameTimeDeltaInMsec` (0.0f)
12. `NVSDK_NGX_Parameter_RayTracingHitDistance` (nullptr)
13. `NVSDK_NGX_Parameter_MotionVectorsReflection` (nullptr)
14. `NVSDK_NGX_Parameter_DLSS_Input_Translucency_SubrectBase_X/Y` (0)
15. `NVSDK_NGX_Parameter_DLSS_Input_Bias_Current_Color_SubrectBase_X/Y` (0)
16. `NVSDK_NGX_Parameter_DLSS_Indicator_Invert_X_Axis/Y_Axis` (0)

While most are optional (nullable), the official helper **always sets ALL of them**
to ensure the parameter map is complete. Some DLSS DLL versions may validate that
certain keys exist in the parameter map, even if their values are null.

## Falcor's Working Implementation (Reference)

File: `Source/RenderPasses/DLSSPass/NGXWrapper.cpp`

```cpp
// 1. Parameter allocation — uses GetCapabilityParameters (one object for all)
NVSDK_NGX_D3D12_GetCapabilityParameters(&mpParameters);

// 2. Feature creation — via helper struct (NOT manual Set calls)
NVSDK_NGX_DLSS_Create_Params dlssParams{};
dlssParams.Feature.InWidth = renderWidth;
dlssParams.Feature.InHeight = renderHeight;
dlssParams.Feature.InTargetWidth = displayWidth;
dlssParams.Feature.InTargetHeight = displayHeight;
dlssParams.Feature.InPerfQualityValue = qualityPreset;
dlssParams.InFeatureCreateFlags =
    NVSDK_NGX_DLSS_Feature_Flags_MVLowRes |
    (isHDR ? NVSDK_NGX_DLSS_Feature_Flags_IsHDR : 0) |
    (depthInverted ? NVSDK_NGX_DLSS_Feature_Flags_DepthInverted : 0);
// NOTE: NO AutoExposure flag

NGX_D3D12_CREATE_DLSS_EXT(cmdList, 1, 1, &mpFeature, mpParameters, &dlssParams);

// 3. Resource barriers BEFORE evaluate
pRenderContext->resourceBarrier(pUnresolvedColor, ShaderResource);
pRenderContext->resourceBarrier(pMotionVectors, ShaderResource);
pRenderContext->resourceBarrier(pDepth, ShaderResource);
pRenderContext->resourceBarrier(pResolvedColor, UnorderedAccess);

// 4. Evaluate — via helper struct (NOT manual Set calls)
NVSDK_NGX_D3D12_DLSS_Eval_Params evalParams{};  // zero-initialized!
evalParams.Feature.pInColor = unresolvedColorResource;
evalParams.Feature.pInOutput = resolvedColorResource;
evalParams.Feature.InSharpness = sharpness;
evalParams.pInDepth = depthResource;
evalParams.pInMotionVectors = motionVectorsResource;
evalParams.InJitterOffsetX = jitterX;
evalParams.InJitterOffsetY = jitterY;
evalParams.InReset = resetAccumulation ? 1 : 0;
evalParams.InRenderSubrectDimensions = { renderWidth, renderHeight };
evalParams.InMVScaleX = motionVectorScaleX;
evalParams.InMVScaleY = motionVectorScaleY;
evalParams.pInExposureTexture = exposureResource;  // nullable

// This internally calls NVSDK_NGX_D3D12_EvaluateFeature_C with ALL parameters
NGX_D3D12_EVALUATE_DLSS_EXT(cmdList, mpFeature, mpParameters, &evalParams);
```

Key differences from UzEngine:
1. Uses `NGX_D3D12_EVALUATE_DLSS_EXT` helper (calls `_C` version internally)
2. Zero-initializes the eval params struct (all nullable pointers = nullptr)
3. No `AutoExposure` flag
4. Explicit resource state transitions before evaluate
5. ALL parameters are set via the helper, including optional nullptr ones

## Step-by-Step Fix for UzEngine

### Step 1: Load the `_C` variant of EvaluateFeature

```cpp
// Change from:
pfnEvaluateFeature = reinterpret_cast<PFN_NVSDK_NGX_D3D12_EvaluateFeature>(
    ::GetProcAddress(ngxModule, "NVSDK_NGX_D3D12_EvaluateFeature"));

// Change to:
pfnEvaluateFeature = reinterpret_cast<PFN_NVSDK_NGX_D3D12_EvaluateFeature>(
    ::GetProcAddress(ngxModule, "NVSDK_NGX_D3D12_EvaluateFeature_C"));
```

Update the typedef to match the C callback:

```cpp
using PFN_NVSDK_NGX_D3D12_EvaluateFeature = NVSDK_NGX_Result(NVSDK_CONV*)(
    ID3D12GraphicsCommandList*, const NVSDK_NGX_Handle*,
    const NVSDK_NGX_Parameter*, PFN_NVSDK_NGX_ProgressCallback_C);
```

### Step 2: Load C Parameter Set Functions from `_nvngx.dll`

Add dynamic loading of the typed Set functions:

```cpp
// New typedefs
using PFN_NVSDK_NGX_Parameter_SetD3d12Resource =
    void(NVSDK_CONV*)(NVSDK_NGX_Parameter*, const char*, ID3D12Resource*);
using PFN_NVSDK_NGX_Parameter_SetF =
    void(NVSDK_CONV*)(NVSDK_NGX_Parameter*, const char*, float);
using PFN_NVSDK_NGX_Parameter_SetUI =
    void(NVSDK_CONV*)(NVSDK_NGX_Parameter*, const char*, unsigned int);
using PFN_NVSDK_NGX_Parameter_SetI =
    void(NVSDK_CONV*)(NVSDK_NGX_Parameter*, const char*, int);

// Load in LoadDll()
pfnSetD3d12Resource = reinterpret_cast<PFN_NVSDK_NGX_Parameter_SetD3d12Resource>(
    ::GetProcAddress(ngxModule, "NVSDK_NGX_Parameter_SetD3d12Resource"));
pfnSetF = reinterpret_cast<PFN_NVSDK_NGX_Parameter_SetF>(
    ::GetProcAddress(ngxModule, "NVSDK_NGX_Parameter_SetF"));
pfnSetUI = reinterpret_cast<PFN_NVSDK_NGX_Parameter_SetUI>(
    ::GetProcAddress(ngxModule, "NVSDK_NGX_Parameter_SetUI"));
pfnSetI = reinterpret_cast<PFN_NVSDK_NGX_Parameter_SetI>(
    ::GetProcAddress(ngxModule, "NVSDK_NGX_Parameter_SetI"));
```

If these are NOT exported from `_nvngx.dll`, fall back to the virtual method approach
but with explicit casts to force the correct overload.

### Step 3: Replace All Virtual Method Calls with C Function Calls

Replace the entire evaluate parameter-setting block:

```cpp
auto* p = m_impl->ngxParams;

// Use C functions to avoid vtable ABI issues
m_impl->pfnSetD3d12Resource(p, NVSDK_NGX_Parameter_Color, colorRes);
m_impl->pfnSetD3d12Resource(p, NVSDK_NGX_Parameter_Output, outputRes);
m_impl->pfnSetD3d12Resource(p, NVSDK_NGX_Parameter_Depth, depthRes);
m_impl->pfnSetD3d12Resource(p, NVSDK_NGX_Parameter_MotionVectors, velocityRes);
m_impl->pfnSetF(p, NVSDK_NGX_Parameter_Jitter_Offset_X, input.jitterX);
m_impl->pfnSetF(p, NVSDK_NGX_Parameter_Jitter_Offset_Y, input.jitterY);
m_impl->pfnSetF(p, NVSDK_NGX_Parameter_Sharpness, sharpness);
m_impl->pfnSetI(p, NVSDK_NGX_Parameter_Reset, input.firstFrame ? 1 : 0);
m_impl->pfnSetF(p, NVSDK_NGX_Parameter_MV_Scale_X, 1.0f);
m_impl->pfnSetF(p, NVSDK_NGX_Parameter_MV_Scale_Y, 1.0f);

// Optional resources — set to nullptr (required by some DLSS versions)
m_impl->pfnSetD3d12Resource(p, NVSDK_NGX_Parameter_TransparencyMask, nullptr);
m_impl->pfnSetD3d12Resource(p, NVSDK_NGX_Parameter_ExposureTexture, nullptr);
m_impl->pfnSetD3d12Resource(p, NVSDK_NGX_Parameter_DLSS_Input_Bias_Current_Color_Mask, nullptr);

// GBuffer surfaces — all nullptr
m_impl->pfnSetD3d12Resource(p, NVSDK_NGX_Parameter_GBuffer_Albedo, nullptr);
m_impl->pfnSetD3d12Resource(p, NVSDK_NGX_Parameter_GBuffer_Roughness, nullptr);
m_impl->pfnSetD3d12Resource(p, NVSDK_NGX_Parameter_GBuffer_Metallic, nullptr);
m_impl->pfnSetD3d12Resource(p, NVSDK_NGX_Parameter_GBuffer_Specular, nullptr);
m_impl->pfnSetD3d12Resource(p, NVSDK_NGX_Parameter_GBuffer_Subsurface, nullptr);
m_impl->pfnSetD3d12Resource(p, NVSDK_NGX_Parameter_GBuffer_Normals, nullptr);
m_impl->pfnSetD3d12Resource(p, NVSDK_NGX_Parameter_GBuffer_ShadingModelId, nullptr);
m_impl->pfnSetD3d12Resource(p, NVSDK_NGX_Parameter_GBuffer_MaterialId, nullptr);
// ... pInAttrib[8] through [15] = nullptr

m_impl->pfnSetUI(p, NVSDK_NGX_Parameter_TonemapperType, 0);
m_impl->pfnSetD3d12Resource(p, NVSDK_NGX_Parameter_MotionVectors3D, nullptr);
m_impl->pfnSetD3d12Resource(p, NVSDK_NGX_Parameter_IsParticleMask, nullptr);
m_impl->pfnSetD3d12Resource(p, NVSDK_NGX_Parameter_AnimatedTextureMask, nullptr);
m_impl->pfnSetD3d12Resource(p, NVSDK_NGX_Parameter_DepthHighRes, nullptr);
m_impl->pfnSetD3d12Resource(p, NVSDK_NGX_Parameter_Position_ViewSpace, nullptr);
m_impl->pfnSetF(p, NVSDK_NGX_Parameter_FrameTimeDeltaInMsec, 0.0f);
m_impl->pfnSetD3d12Resource(p, NVSDK_NGX_Parameter_RayTracingHitDistance, nullptr);
m_impl->pfnSetD3d12Resource(p, NVSDK_NGX_Parameter_MotionVectorsReflection, nullptr);

// Subrect dimensions and offsets
m_impl->pfnSetUI(p, NVSDK_NGX_Parameter_DLSS_Input_Color_Subrect_Base_X, 0);
m_impl->pfnSetUI(p, NVSDK_NGX_Parameter_DLSS_Input_Color_Subrect_Base_Y, 0);
m_impl->pfnSetUI(p, NVSDK_NGX_Parameter_DLSS_Input_Depth_Subrect_Base_X, 0);
m_impl->pfnSetUI(p, NVSDK_NGX_Parameter_DLSS_Input_Depth_Subrect_Base_Y, 0);
m_impl->pfnSetUI(p, NVSDK_NGX_Parameter_DLSS_Input_MV_SubrectBase_X, 0);
m_impl->pfnSetUI(p, NVSDK_NGX_Parameter_DLSS_Input_MV_SubrectBase_Y, 0);
m_impl->pfnSetUI(p, NVSDK_NGX_Parameter_DLSS_Input_Translucency_SubrectBase_X, 0);
m_impl->pfnSetUI(p, NVSDK_NGX_Parameter_DLSS_Input_Translucency_SubrectBase_Y, 0);
m_impl->pfnSetUI(p, NVSDK_NGX_Parameter_DLSS_Input_Bias_Current_Color_SubrectBase_X, 0);
m_impl->pfnSetUI(p, NVSDK_NGX_Parameter_DLSS_Input_Bias_Current_Color_SubrectBase_Y, 0);
m_impl->pfnSetUI(p, NVSDK_NGX_Parameter_DLSS_Output_Subrect_Base_X, 0);
m_impl->pfnSetUI(p, NVSDK_NGX_Parameter_DLSS_Output_Subrect_Base_Y, 0);
m_impl->pfnSetUI(p, NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Width, renderW);
m_impl->pfnSetUI(p, NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Height, renderH);
m_impl->pfnSetF(p, NVSDK_NGX_Parameter_DLSS_Pre_Exposure, 1.0f);
m_impl->pfnSetF(p, NVSDK_NGX_Parameter_DLSS_Exposure_Scale, 1.0f);
m_impl->pfnSetI(p, NVSDK_NGX_Parameter_DLSS_Indicator_Invert_X_Axis, 0);
m_impl->pfnSetI(p, NVSDK_NGX_Parameter_DLSS_Indicator_Invert_Y_Axis, 0);
```

### Step 4: Remove AutoExposure Flag from Feature Creation

```cpp
// Change from:
ngxParams->Set(NVSDK_NGX_Parameter_DLSS_Feature_Create_Flags,
    NVSDK_NGX_DLSS_Feature_Flags_IsHDR |
    NVSDK_NGX_DLSS_Feature_Flags_MVLowRes |
    NVSDK_NGX_DLSS_Feature_Flags_DepthInverted |
    NVSDK_NGX_DLSS_Feature_Flags_AutoExposure);

// Change to:
ngxParams->Set(NVSDK_NGX_Parameter_DLSS_Feature_Create_Flags,
    NVSDK_NGX_DLSS_Feature_Flags_IsHDR |
    NVSDK_NGX_DLSS_Feature_Flags_MVLowRes |
    NVSDK_NGX_DLSS_Feature_Flags_DepthInverted);
```

### Step 5: Add Resource State Transitions Before Evaluate

Ensure all input resources are in Shader Resource state and output in UAV state:

```cpp
// Before evaluate call
dx12Cmd->ResourceBarrier(colorRes, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
dx12Cmd->ResourceBarrier(depthRes, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
dx12Cmd->ResourceBarrier(velocityRes, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
dx12Cmd->ResourceBarrier(outputRes, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
```

### Step 6: Also use C functions for Feature Creation parameters

Apply the same C function approach to the `CreateFeature` parameter setup to ensure
consistency. The virtual method issue affects ALL `Set` calls, not just evaluate.

## Summary of Differences Between UzEngine and Working Implementations

| Aspect | UzEngine (broken) | Falcor / Official Helper (working) |
|---|---|---|
| Parameter Set method | C++ virtual `p->Set()` | C function `NVSDK_NGX_Parameter_SetD3d12Resource()` |
| EvaluateFeature variant | `NVSDK_NGX_D3D12_EvaluateFeature` | `NVSDK_NGX_D3D12_EvaluateFeature_C` |
| AutoExposure flag | Yes | No |
| Optional nullptr params | Missing most | ALL set (even if nullptr) |
| Resource barriers | Not explicit before evaluate | Explicit SRV/UAV transitions |
| Struct zero-init | N/A (manual Set calls) | `NVSDK_NGX_D3D12_DLSS_Eval_Params{}` (zero-init) |

## Sources

- [NVIDIA DLSS SDK nvsdk_ngx_helpers.h](https://github.com/NVIDIA/DLSS/blob/main/include/nvsdk_ngx_helpers.h)
- [NVIDIA DLSS SDK nvsdk_ngx.h](https://github.com/NVIDIA/DLSS/blob/main/include/nvsdk_ngx.h)
- [NVIDIA DLSS SDK nvsdk_ngx_defs.h](https://github.com/NVIDIA/DLSS/blob/main/include/nvsdk_ngx_defs.h)
- [Falcor NGXWrapper.cpp](https://github.com/NVIDIAGameWorks/Falcor/blob/master/Source/RenderPasses/DLSSPass/NGXWrapper.cpp)
- [NGX SDK Integration (DeepWiki)](https://deepwiki.com/NVIDIA/DLSS/4-ngx-sdk-integration)
- [NVIDIA NGX Programming Guide](https://docs.nvidia.com/ngx/latest/programming-guide/index.html)
- Local SDK headers: `runtime/thirdparty/dlss/include/nvsdk_ngx_params.h` (vtable layout)
- Local SDK lib: `runtime/thirdparty/dlss/lib/Windows_x86_64/x64/nvsdk_ngx_d.lib` (symbol exports)
