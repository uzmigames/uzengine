# UzEngine · CB Layouts for the New Lighting Pipeline

Concrete constant-buffer memory layouts for the new passes. HLSL and
C++ must match exactly — DXC pads `float3` to 16 bytes, aligns arrays to
16-byte boundaries, etc. Get this wrong and everything renders black.

References: [../../gpu-backend/rhi/ue5-cbuffer-layout-dx12-vulkan-analysis.md](../../gpu-backend/rhi/ue5-cbuffer-layout-dx12-vulkan-analysis.md)
and project memory on "HLSL cbuffer alignment (DXC aligns float4 arrays to 16B)".

---

## 1 · Rules (recap)

- Every scalar (float, uint, int) is 4 bytes, aligned to 4.
- Every float2 is 8 bytes, aligned to 8.
- Every float3 is 12 bytes of data but **aligned to 16** (so it effectively consumes 16 B unless a float follows).
- Every float4 / float4x4 / array entry aligns to 16.
- Structs end on a 16-byte boundary.
- The overall CB size must be a multiple of 16.

Validate with `dxc -Fc out.asm shader.hlsl`; if C++ side disagrees, DXC
will tell you in the dumped assembly.

---

## 2 · IndirectIntegrationCB

Used by `indirect_integration_cs.hlsl`.

### HLSL

```hlsl
cbuffer IndirectIntegrationCB : register(b0)
{
    float4x4 InvViewProj;         //   0 (64 B)
    float3   CamPos;              //  64 (aligned 16, consumes 16 with pad)
    float    IndirectLightingIntensity;
    float3   DDGIOrigin;          //  80 (16 B slot, 3 floats + pad)
    float    DDGIProbeSpacing;
    int3     DDGIProbeCount;      //  96 (16 B slot, 3 ints + pad)
    float    SkyIntensity;
    float2   ScreenSize;          // 112 (8 B, aligned 8)
    float2   InvScreenSize;       // 120
    float    NearZ;               // 128
    float    FarZ;
    float    TAAAlpha;
    float    _pad0;               // total: 144 B, multiple of 16 ✓
};
```

### C++ mirror

```cpp
// runtime/engine/renderer/passes/lighting/constant_buffers.h
struct IndirectIntegrationCB
{
    Matrix4x4  invViewProj;                 //   0
    Vector3    camPos;                       //  64
    float      indirectLightingIntensity;    //  76
    Vector3    ddgiOrigin;                   //  80
    float      ddgiProbeSpacing;             //  92
    IVec3      ddgiProbeCount;               //  96
    float      skyIntensity;                 // 108
    Vector2    screenSize;                   // 112
    Vector2    invScreenSize;                // 120
    float      nearZ;                        // 128
    float      farZ;                         // 132
    float      taaAlpha;                     // 136
    float      _pad0;                        // 140
};
static_assert(sizeof(IndirectIntegrationCB) == 144,
              "IndirectIntegrationCB size must match HLSL layout");
```

---

## 3 · ReSTIRDIReservoirCB

Used by ReSTIR DI candidate-gen, visibility, temporal, spatial passes.

### HLSL

```hlsl
cbuffer ReSTIRDIReservoirCB : register(b0)
{
    float4x4 ViewProj;               //  0 (64)
    float4x4 PrevViewProj;           // 64 (64)
    float3   CamPos;                 // 128
    uint     FrameIdx;               // 140
    uint2    ScreenSize;             // 144 (8)
    uint     LightCount;             // 152
    uint     CandidateCount;         // 156  (typ 32)
    uint     SpatialNeighbors;       // 160  (typ 5)
    float    SpatialRadius;          // 164  (in pixels)
    uint     MaxM;                   // 168  (typ 20)
    float    _pad0;                  // 172
    float4   JitterRotation;         // 176  (4 floats for 2D rotation matrix)
                                     // total: 192
};
```

### C++ mirror

```cpp
struct ReSTIRDIReservoirCB
{
    Matrix4x4 viewProj;
    Matrix4x4 prevViewProj;
    Vector3   camPos;
    uint32_t  frameIdx;
    UVec2     screenSize;
    uint32_t  lightCount;
    uint32_t  candidateCount;
    uint32_t  spatialNeighbors;
    float     spatialRadius;
    uint32_t  maxM;
    float     _pad0;
    Vector4   jitterRotation;
};
static_assert(sizeof(ReSTIRDIReservoirCB) == 192, "");
```

---

## 4 · LTCAreaLightCB

Per-draw or per-frame CB for LTC rect/disc/tube lights.

### HLSL

```hlsl
struct LTCLightGPU
{
    float3 p0;          //   0 (16 slot)
    float  _pad0;       //  12
    float3 p1;          //  16
    float  _pad1;       //  28
    float3 p2;          //  32
    float  _pad2;       //  44
    float3 p3;          //  48
    float  _pad3;       //  60
    float3 color;       //  64
    float  intensity;   //  76
    uint   shape;       //  80 (0=rect, 1=disc, 2=tube, 3=sphere)
    float  width;       //  84
    float  height;      //  88
    float  radius;      //  92  (tube/sphere)
                        // total: 96, multiple of 16 ✓
};

cbuffer LTCAreaLightCB : register(b1)
{
    uint         AreaLightCount;     //   0
    uint3        _pad0;              //   4
    LTCLightGPU  Lights[64];         //  16  (64 × 96 = 6144)
};                                    // total: 6160, needs pad to 6160... hmm
                                      // actually 6160 % 16 = 0 ✓
```

### C++ mirror

```cpp
struct LTCLightGPU
{
    Vector3 p0; float _pad0;
    Vector3 p1; float _pad1;
    Vector3 p2; float _pad2;
    Vector3 p3; float _pad3;
    Vector3 color; float intensity;
    uint32_t shape;
    float width, height, radius;
};
static_assert(sizeof(LTCLightGPU) == 96, "");

struct LTCAreaLightCB
{
    uint32_t areaLightCount;
    uint32_t _pad0[3];
    LTCLightGPU lights[64];
};
static_assert(sizeof(LTCAreaLightCB) == 16 + 64 * 96, "");
```

---

## 5 · DDGIProbeUpdateCB

Used by the DDGI probe update compute shader.

### HLSL

```hlsl
cbuffer DDGIProbeUpdateCB : register(b0)
{
    float3   ProbeGridOrigin;    //   0
    float    ProbeSpacing;       //  12
    uint3    ProbeCount;         //  16
    uint     RaysPerProbe;       //  28
    float    HysteresisBlend;    //  32
    float    MaxRayDistance;     //  36
    uint     FrameProbeOffset;   //  40  (rotating update)
    uint     NumProbesThisFrame; //  44
    float4x4 LightDirSpace;      //  48  (sun transform into shadow map space)
    float3   SunDirection;       // 112
    float    SunIntensity;       // 124
    float3   SkyColor;           // 128
    float    SkyIntensity;       // 140
                                 // total: 144
};
```

### C++ mirror

```cpp
struct DDGIProbeUpdateCB
{
    Vector3 probeGridOrigin;  float probeSpacing;
    UVec3   probeCount;       uint32_t raysPerProbe;
    float   hysteresisBlend;
    float   maxRayDistance;
    uint32_t frameProbeOffset;
    uint32_t numProbesThisFrame;
    Matrix4x4 lightDirSpace;
    Vector3 sunDirection;     float sunIntensity;
    Vector3 skyColor;         float skyIntensity;
};
static_assert(sizeof(DDGIProbeUpdateCB) == 144, "");
```

---

## 6 · SSGIDenoiseCB

Used by à-trous SSGI denoise.

### HLSL

```hlsl
cbuffer SSGIDenoiseCB : register(b0)
{
    uint2 ScreenSize;            //   0
    float DepthThreshold;        //   8
    float NormalThreshold;       //  12
    float LuminanceThreshold;    //  16
    float _pad0;                 //  20
    uint2 _pad1;                 //  24
    int   StepSize;              //  32  (0, 1, 2, 4, 8 for à-trous stages)
    int   _pad2;                 //  36
    uint2 _pad3;                 //  40
                                 // total: 48
};
```

### C++ mirror

```cpp
struct SSGIDenoiseCB
{
    UVec2 screenSize;
    float depthThreshold;
    float normalThreshold;
    float luminanceThreshold;
    float _pad0;
    UVec2 _pad1;
    int32_t stepSize;
    int32_t _pad2;
    UVec2 _pad3;
};
static_assert(sizeof(SSGIDenoiseCB) == 48, "");
```

---

## 7 · Validation

Every CB should have a unit test:

```cpp
// tests/constant_buffer_layouts_test.cpp
TEST(CBLayouts, IndirectIntegrationCB) {
    EXPECT_EQ(sizeof(IndirectIntegrationCB), 144u);
    EXPECT_EQ(offsetof(IndirectIntegrationCB, camPos), 64u);
    EXPECT_EQ(offsetof(IndirectIntegrationCB, ddgiOrigin), 80u);
    // ... etc
}
```

Run `clang-tidy` with layout-check flags on the headers. Compile with
`-Wpadded` and fix warnings.

---

## 8 · Cross-checks

1. **dxc -Fc check**: Compile each shader with `dxc -Fc output.asm`;
   the asm header lists `cbuffer` layout with offsets. Compare to C++.

2. **RenderDoc binding check**: Capture a frame, open the constant
   buffer bind in RenderDoc. Verify every field shows the expected value.

3. **RHI assert in debug**: On every CB upload, validate that
   `sizeof(cb) == uploadSize`. Catches drift early.

---

## 9 · Tooling

Recommended: a small helper that auto-generates the C++ struct from a
single "CB spec" file (YAML/JSON) + validates it against the shader's
DXC asm dump. Saves drift.

For now, maintaining both sides manually with `static_assert` is
acceptable for our team size.

---

## 10 · References

- HLSL cbuffer packing rules: learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dx-graphics-hlsl-packing-rules
- [../../gpu-backend/rhi/ue5-cbuffer-layout-dx12-vulkan-analysis.md](../../gpu-backend/rhi/ue5-cbuffer-layout-dx12-vulkan-analysis.md) — how UE5 handles CB layouts.
- Internal memory: "HLSL cbuffer alignment (DXC aligns float4 arrays to 16B)" — root-cause from past bug.
