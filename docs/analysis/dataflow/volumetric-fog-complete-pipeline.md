# Data Flow: Volumetric Fog Energy Balance Pipeline

## Complete UE5 Implementation Path

From component property → constant buffer → GPU shader → final pixel color.

---

## Stage 1: Component Properties (CPU Scene)

**File**: `ExponentialHeightFogComponent.h:142-176`

```
Component Properties (Editable in Editor)
├── VolumetricFogAlbedo (FColor) 
│   └─ Default: (255, 255, 255) = (1.0, 1.0, 1.0) in [0,1] range
│   └─ Represents: Single-scattering albedo (no absorption by default)
│
├── VolumetricFogEmissive (FLinearColor)
│   └─ Default: (0, 0, 0)
│   └─ Represents: Light self-emitted by fog density
│   └─ KEY: Scaled by density in material setup to compensate for extinction
│
├── VolumetricFogExtinctionScale (float)
│   └─ Default: 1.0f, Range: [0.1, 10.0]
│   └─ Represents: Multiplier on height fog density to control opacity
│
├── VolumetricFogScatteringDistribution (float)
│   └─ Default: 0.0f, Range: [-0.9, 0.9]
│   └─ Represents: Henyey-Greenstein phase function parameter (direction only)
│
├── FogDensity (float)
│   └─ Default: 0.02f, Range: [0, 0.05]
│   └─ Represents: Global exponential fog density multiplier
│   └─ KEY: Very low default prevents over-darkening
│
└── FogHeightFalloff (float)
    └─ Default: 0.2f, Range: [0.001, 2.0]
    └─ Represents: Rate at which density decreases with altitude
```

---

## Stage 2: CPU-Side Setup (InitFogConstants)

**File**: `FogRendering.cpp:208-304`

**Function**: `FSceneRenderer::InitFogConstants()`

```
For each view:
    Read ExponentialHeightFogComponent from scene
    
    Compute collapsed fog parameters:
    └─ CollapsedFogParameter[0] = FogDensity × exp2(-FogHeightFalloff × (CameraHeight - FogHeight))
       └─ This pre-computes the exponential term at camera height
       └─ Result: [0.02 × exp2(-0.2 × height_diff)]
    
    Compute observer height clamp:
    └─ MaxObserverHeight = min(CameraZ, MinZ + 65536)
       └─ Prevents numerical precision issues
    
    Fill view constants:
    ├─ View.ExponentialFogParameters = (CollapsedFogParameter[0], FogHeightFalloff, MaxObserverHeight, StartDistance)
    ├─ View.ExponentialFogParameters2 = (CollapsedFogParameter[1], SecondHeightFalloff, SecondDensity, SecondHeight)
    ├─ View.ExponentialFogParameters3 = (FogDensity, FogHeight, HasCubemap ? 1.0 : 0.0, CutoffDistance)
    ├─ View.DirectionalInscatteringColor = FogInfo.DirectionalInscatteringColor × SunLight.Luminance
    ├─ View.VolumetricFogAlbedo = (VolumetricFogAlbedo.R, .G, .B) / 255.0f  ← Normalized to [0,1]
    ├─ View.VolumetricFogPhaseG = VolumetricFogScatteringDistribution
    ├─ View.VolumetricFogExtinctionScale = VolumetricFogExtinctionScale
    └─ View.VolumetricFogEmissive = VolumetricFogEmissive (raw HDR color)
```

---

## Stage 3: Constant Buffer Upload

**File**: `FogRendering.cpp:63-115`

**Function**: `SetupFogUniformParameters()`

```
Create FFogUniformParameters struct:

    ├─ ExponentialFogParameters (from View, filled in InitFogConstants)
    ├─ ExponentialFogParameters2
    ├─ ExponentialFogParameters3
    ├─ ExponentialFogColorParameter = (FogColor.R, .G, .B, 1.0 - FogMaxOpacity)
    │  └─ Alpha component stores (1 - MaxOpacity), used in height fog shader
    ├─ DirectionalInscatteringColor
    ├─ SkyAtmosphereAmbientContributionColorScale
    │
    ├─ Volumetric Fog specifics:
    ├─ IntegratedLightScattering = 3D texture (or black if no vfog)
    ├─ VolumetricFogAlbedo = (R/255, G/255, B/255)  ← Normalized
    ├─ VolumetricFogPhaseG = ScatteringDistribution
    ├─ VolumetricFogExtinctionScale = ExtinctionScale  ← Direct copy
    ├─ VolumetricFogEmissive = Emissive  ← Direct copy, will be scaled by density in shader
    └─ ApplyVolumetricFog = (IntegratedLightScattering != null) ? 1.0 : 0.0

Bind to shader as "FogStruct" uniform buffer
```

---

## Stage 4a: Material Setup Shader (Density/Extinction)

**File**: `VolumetricFog.usf:MaterialSetupCS`

**Line 123-252**: Compute shader that fills density grid

```
Input per voxel:
    GridCoordinate (uint3) from [numthreads(4,4,4)]
    └─ 3D coordinate in froxel grid (e.g., 128×128×64)

Step 1: Compute world position of voxel center
    LineIntegral from line 135-137:
    ├─ Float GlobalDensityFirst = FogStruct.ExponentialFogParameters3.x 
    │                              × exp2(-FogStruct.ExponentialFogParameters.y 
    │                                      × (WorldPosition.z - FogStruct.ExponentialFogParameters3.y))
    │  
    │  Breakdown:
    │  ├─ FogStruct.ExponentialFogParameters3.x = FogDensity (0.02)
    │  ├─ FogStruct.ExponentialFogParameters.y = FogHeightFalloff (0.2)
    │  ├─ WorldPosition.z = Voxel's world Z position (height)
    │  ├─ FogStruct.ExponentialFogParameters3.y = FogHeight (height offset)
    │  └─ Result: density at this height
    │
    └─ Same for second fog layer (lines 136-137)

Step 2: *** THE CRITICAL ANTI-DARKENING FACTOR (LINE 142) ***
    ├─ GlobalDensity *= 0.5f
    │  └─ Matches volumetric representation to analytical height fog
    │  └─ Prevents double-darkening since analytical fog is also applied
    │
    └─ This factor accounts for voxel integration vs. analytical integration

Step 3: Compute Extinction and Scattering (LINE 144-146)
    ├─ float Extinction = max(GlobalDensity × GlobalExtinctionScale, 0)
    │  └─ GlobalExtinctionScale = View.VolumetricFogExtinctionScale
    │  └─ Extinction = amount of light absorbed per unit distance
    │
    └─ float3 Scattering = Albedo × Extinction
       ├─ Albedo = GlobalAlbedo.rgb (from VolumetricFogAlbedo, normalized)
       ├─ Scattering = amount of absorbed light that scatters back
       ├─ KEY: Scattering ≤ Extinction (because Albedo ≤ 1.0)
       └─ When Albedo = 1.0 (default): Scattering = Extinction
          └─ No net absorption, all light is scattered back

Step 4: Emissive Compensation (LINE 244-249)
    ├─ float3 HeightFogEmissive = GlobalEmissive.rgb (from VolumetricFogEmissive)
    │
    └─ IF PROJECT_EXPFOG_MATCHES_VFOG:
       └─ HeightFogEmissive.rgb *= GlobalDensity
          └─ *** CRITICAL ***: Emissive scales with density
          └─ Where extinction is high, emissive is also high
          └─ This compensates for light lost to extinction
          └─ Net result: No over-darkening

Step 5: Output to RWVBufferA and RWVBufferB (LINE 242-249)
    ├─ RWVBufferA[GridCoordinate] = float4(Scattering, Extinction)
    │  └─ Format: (R, G, B for scattering, A for extinction)
    │  └─ Scattering: directional scattering accumulator
    │  └─ Extinction: transmittance basis (used in integration)
    │
    └─ RWVBufferB[GridCoordinate] = float4(HeightFogEmissive + AdditionalEmissive, 0)
       └─ Stores emissive light to be added during integration
```

---

## Stage 4b: Light Injection Shader

**File**: `VolumetricFog.usf:InjectShadowedLocalLightCommon`

**Lines 365-493**: For each light, add scattering to grid

```
For each light in scene:
    For each voxel in grid:
        
        Step 1: Compute light geometry
        ├─ TranslatedWorldPosition = voxel world position
        ├─ ToLight = light.position - TranslatedWorldPosition
        ├─ DistanceToLight = length(ToLight)
        ├─ L = normalize(ToLight)
        └─ CameraVector = direction from camera to voxel

        Step 2: Compute light attenuation
        ├─ If rect light:
        │  └─ Attenuation = IntegrateLightArea(...)
        └─ Else (point/spot):
           └─ Attenuation = InverseSquaredFalloff(distance)
           └─ With bias to prevent extreme aliasing near light

        Step 3: Compute phase function (LINE 257-259)
        ├─ phase = HenyeyGreenstein(PhaseG, dot(L, -CameraVector))
        │  ├─ PhaseG = View.VolumetricFogPhaseG (ScatteringDistribution, default 0.0)
        │  ├─ dot(L, -CameraVector) = cosine of angle to camera
        │  ├─ When PhaseG = 0.0: isotropic (same in all directions)
        │  ├─ When PhaseG > 0.0: forward scattering (brighter toward light direction)
        │  └─ NOTE: Phase function affects DIRECTION only, not brightness
        │           Higher phase G doesn't make fog brighter, just more directional

        Step 4: Compute shadow factor (IF ENABLED_SHADOW_COMPUTATION)
        ├─ ShadowFactor = ComputeVolumeShadowing(...)
        ├─ Reads shadow atlas or uses distance field
        ├─ Returns [0, 1]: 0 = fully shadowed, 1 = fully lit
        └─ If no shadows available: ShadowFactor = 1.0

        Step 5: Accumulate light contribution (LINE 482)
        ├─ OutScattering.rgb += LightColor 
        │                       × Phase 
        │                       × Attenuation 
        │                       × ShadowFactor 
        │                       × VolumetricScatteringIntensity
        │
        │  Where:
        │  ├─ LightColor = light's RGB color
        │  ├─ Phase = directional weighting (Henyey-Greenstein)
        │  ├─ Attenuation = light falloff
        │  ├─ ShadowFactor = shadow visibility
        │  ├─ VolumetricScatteringIntensity = per-light intensity parameter
        │  └─ Result: Added to grid voxel
        │
        └─ This accumulates per-light scattering
           └─ At the end, integrate all light contributions

        Step 6: Pre-expose result (LINE 489)
        └─ OutScattering.rgb *= View.PreExposure / NumSuperSamples
           └─ Pre-exposure: tone mapping in reverse (brighten for post-exposure compensation)
```

---

## Stage 5: Integration Shader (Raymarching)

**File**: `VolumetricFog.usf` (FinalIntegrationCS, not shown in excerpt)

**Purpose**: Integrate scattering and extinction along view ray

```
For each pixel in screen:
    For each slice along view ray (from near to far):
        
        Step 1: Sample material (scattering, extinction) at voxel
        ├─ Scattering = RWVBufferA[voxel].rgb
        ├─ Extinction = RWVBufferA[voxel].a
        └─ Result: Amount of light scattered + absorbed at this slice

        Step 2: Sample injected light from RWLightScattering
        ├─ LightScattering = RWLightScattering[voxel].rgb
        │  └─ Accumulated from all light injection passes
        └─ Result: Light contributed by all lights

        Step 3: Sample emissive
        ├─ Emissive = RWVBufferB[voxel].rgb
        │  └─ Already scaled by density if PROJECT_EXPFOG_MATCHES_VFOG
        └─ Result: Self-emitted light

        Step 4: Accumulate along ray
        ├─ TransmittanceToThisPoint = exp2(-sum(Extinction from near to here))
        ├─ TransmittanceAfterThisPoint = exp2(-Extinction of this voxel × thickness)
        │
        ├─ InScatteredLight += TransmittanceToThisPoint 
        │                      × (LightScattering + Emissive + LocalScattering) 
        │                      × (1 - TransmittanceAfterThisPoint)
        │
        │  Explanation:
        │  ├─ TransmittanceToThisPoint: how much light reaches this voxel
        │  ├─ (LightScattering + Emissive): light available at voxel
        │  ├─ (1 - TransmittanceAfterThisPoint): fraction of light scattered (not transmitted)
        │  └─ Product: light that reaches camera from this voxel
        │
        └─ FinalTransmittance *= TransmittanceAfterThisPoint
           └─ Keep track of cumulative light loss

        Step 5: Temporal reprojection (OPTIONAL)
        ├─ HistoryAlpha = 0.9f (from View.VolumetricFogHistoryWeight)
        ├─ PreviousFrameValue = SampleHistory3D(PreviousFrame, UV)
        ├─ CurrentValue = integrate current frame
        └─ Result = lerp(CurrentValue, PreviousFrameValue, HistoryAlpha)
           └─ Reduces jitter, smooths under-sampling artifacts

        Step 6: Output to IntegratedLightScattering texture
        └─ IntegratedLightScattering[screenCoord] = float4(InScatteredLight, FinalTransmittance)
           └─ Ready for composition pass
```

---

## Stage 6: Composition Shader (Pixel)

**File**: `HeightFogCommon.ush:CombineVolumetricFog`

**Lines 420-470**: Blend volumetric + height fog + scene

```
For each pixel:
    
    Step 1: Get scene depth and world position
    ├─ SceneDepth = scene depth buffer
    ├─ WorldPosition = reproject from depth
    └─ Result: Where in world is this pixel?

    Step 2: Look up volumetric fog in 3D texture
    ├─ VolumeUV = ComputeVolumeUV(WorldPosition, View.WorldToClip)
    │  └─ Convert world position to froxel grid coordinates
    ├─ VolumetricFogLookup = Sample3D(IntegratedLightScattering, VolumeUV)
    │  └─ VolumetricFogLookup.rgb = in-scattered light (HDR)
    │  └─ VolumetricFogLookup.a = transmittance [0, 1]
    └─ Result: Fog light + transmittance at this pixel

    Step 3: Get height fog
    ├─ HeightFog = GetExponentialHeightFog(WorldPosition)
    │  └─ Uses analytical formula (exponential integration)
    │  └─ HeightFog.rgb = inscattering color
    │  └─ HeightFog.a = transmittance [0, 1]
    └─ Result: Analytical fog

    Step 4: *** CRITICAL COMPOSITION (LINE 469) ***
    ├─ FinalColor.rgb = VolumetricFogLookup.rgb 
    │                   + HeightFog.rgb × VolumetricFogLookup.a
    │                                    ^^^^^^^^^^^^^^^^^^^
    │                                    Transmittance of volumetric fog
    │                                    Controls how much height fog shows through
    │
    │  Breakdown:
    │  ├─ VolumetricFogLookup.rgb: In-scattered light from volumetric fog
    │  ├─ HeightFog.rgb: Inscattering color from analytical height fog
    │  ├─ VolumetricFogLookup.a: Transmittance of volumetric fog
    │  │  └─ If = 0.0 (opaque): Only volumetric fog visible, height fog blocked
    │  │  └─ If = 1.0 (clear): Height fog fully visible through volumetric fog
    │  ├─ Product HeightFog.rgb × VolumetricFogLookup.a:
    │  │  └─ Height fog contribution weighted by vfog transparency
    │  └─ Sum: Total fog color visible at pixel
    │
    └─ FinalAlpha = VolumetricFogLookup.a × HeightFog.a
       ├─ Multiplicative: Both must transmit for pixel to be clear
       ├─ VolumetricFogLookup.a: How much light passes through vfog
       ├─ HeightFog.a: How much light passes through height fog
       └─ Product: Final transmittance (used to blend with scene)

    Step 5: Blend with scene
    ├─ SceneColor = render target (color + lighting)
    ├─ FinalColor = lerp(SceneColor, FogColor, 1 - FinalAlpha)
    │              or equivalently:
    │            = FogColor × (1 - FinalAlpha) + SceneColor × FinalAlpha
    └─ Result: Fog blended over scene
```

**The Anti-Darkening Magic**:
```
When volumetric fog is OPAQUE (transmittance = 0):
    FinalColor = VolumetricFogLookup.rgb + HeightFog.rgb × 0
               = VolumetricFogLookup.rgb
    FinalAlpha = 0
    
    Scene is completely replaced by fog
    BUT fog color comes from:
    - Light scattering (bright when illuminated)
    - Emissive compensation (scales with density)
    
    Net result: OPAQUE BUT NOT DARK
    Because light is being added (scattering + emissive)
    even though transmittance is zero

When volumetric fog is CLEAR (transmittance = 1):
    FinalColor = VolumetricFogLookup.rgb + HeightFog.rgb × 1
               = VolumetricFogLookup.rgb + HeightFog.rgb
    FinalAlpha = 1
    
    Scene is fully visible
    But fog light is added on top (additive contribution)
    
    Net result: ENHANCEMENT, not darkening
```

---

## Complete Parameter Flow Diagram

```
ExponentialHeightFogComponent (Editor)
    ├─ FogDensity = 0.02f
    ├─ FogHeightFalloff = 0.2f
    ├─ VolumetricFogAlbedo = (255, 255, 255)
    ├─ VolumetricFogEmissive = (0, 0, 0)
    ├─ VolumetricFogExtinctionScale = 1.0f
    └─ VolumetricFogScatteringDistribution = 0.0f
              ↓
    InitFogConstants (CPU)
    ├─ CollapsedFogParameter = FogDensity × exp2(...)
    ├─ View.ExponentialFogParameters = (CollapsedFogParameter, FogHeightFalloff, ...)
    ├─ View.VolumetricFogAlbedo = (255, 255, 255) / 255 = (1.0, 1.0, 1.0)
    ├─ View.VolumetricFogEmissive = (0, 0, 0)
    ├─ View.VolumetricFogExtinctionScale = 1.0f
    └─ View.VolumetricFogPhaseG = 0.0f
              ↓
    FFogUniformParameters (GPU Constant Buffer)
    ├─ ExponentialFogParameters3.x = FogDensity
    ├─ ExponentialFogParameters.y = FogHeightFalloff
    ├─ GlobalAlbedo = (1.0, 1.0, 1.0)
    ├─ GlobalEmissive = (0, 0, 0)
    ├─ GlobalExtinctionScale = 1.0f
    └─ PhaseG = 0.0f
              ↓
    MaterialSetupCS (Compute Shader)
    ├─ GlobalDensity = FogDensity × exp2(-FogHeightFalloff × worldZ) × 0.5f
    ├─ Extinction = GlobalDensity × ExtinctionScale
    ├─ Scattering = Albedo × Extinction
    ├─ Emissive = GlobalEmissive
    └─ Output: RWVBufferA(Scattering, Extinction), RWVBufferB(Emissive)
              ↓
    Light Injection (for each light)
    └─ Add light color × phase × attenuation × shadow to RWLightScattering
              ↓
    Integration (Raymarching)
    └─ Accumulate along ray: InScatteredLight + Emissive + LightScattering
              ↓
    IntegratedLightScattering Texture (3D, RWTexture)
    ├─ .rgb = in-scattered light (sum of scattering + emissive + light contributions)
    └─ .a = transmittance
              ↓
    Composition Shader (Pixel)
    ├─ VolumetricFog = sample IntegratedLightScattering
    ├─ HeightFog = ComputeExponentialHeightFog(...)
    └─ Final = VolumetricFog + HeightFog × VolumetricFog.alpha
              ↓
    Final Pixel
    └─ Fog blended with scene using final transmittance
```

---

## Energy Conservation Check

**At each stage, verify**:

1. **Material Setup**:
   - `Scattering ≤ Extinction` ✓ (Albedo ≤ 1.0)
   - `Scattering = Albedo × Extinction` ✓ (Decoupled)

2. **Light Injection**:
   - Phase function is normalized ✓ (Henyey-Greenstein)
   - Attenuation ∈ [0, 1] ✓ (Light falloff)

3. **Integration**:
   - Transmittance monotonically decreases along ray ✓
   - In-scattered light accumulates correctly ✓

4. **Composition**:
   - Height fog only visible through transparent vfog ✓
   - Final alpha is product of both alphas ✓

If all checks pass, fog won't over-darken!

---

## Key Values for UzEngine Implementation

| Stage | Variable | Value | Source |
|-------|----------|-------|--------|
| Component | `FogDensity` | 0.02f | ExponentialHeightFogData.h:18 |
| Component | `VolumetricFogAlbedo` | (1, 1, 1) | Default (white) |
| Component | `VolumetricFogEmissive` | (0, 0, 0) | Default (off) |
| Shader | `MatchHeightFogFactor` | 0.5f | VolumetricFog.usf:142 |
| Shader | `Scattering` | `Albedo × Extinction` | VolumetricFog.usf:146 |
| Shader | `Emissive scale` | `× GlobalDensity` | VolumetricFog.usf:246 |
| Composition | Final color | `VFog + HeightFog × VFog.a` | HeightFogCommon.ush:469 |

