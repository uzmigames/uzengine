<!-- doc-meta
title: Quality Scalability System
phase: 3a
priority: P1
status: proposed
created: 2026-04-01
-->

# Quality Scalability System

## Motivation

UzEngine currently has hardcoded rendering quality settings scattered across CVars and code.
There's no unified system to switch between quality presets (Low/Medium/High/Ultra/Cinematic)
that affects shadows, textures, view distance, post-processing, and effects together.

UE5's Scalability system (`sg.ShadowQuality`, `sg.TextureQuality`, etc.) provides the
reference architecture: named quality levels (0-4) that map to CVar bundles.

## Design (UE5 Scalability Reference)

### Quality Levels

| Level | Name | Target |
|-------|------|--------|
| 0 | Low | Integrated GPU, 30 FPS target |
| 1 | Medium | Entry GPU, 60 FPS target |
| 2 | High | Mid-range GPU, 60 FPS |
| 3 | Ultra | High-end GPU, unlocked FPS |
| 4 | Cinematic | RTX 4090+, visual fidelity over FPS |

### Scalability Groups

Each group independently controls one aspect of rendering quality:

#### 1. Shadow Quality (`r.Shadow.*`)
| CVar | Low | Medium | High | Ultra | Cinematic |
|------|-----|--------|------|-------|-----------|
| `r.Shadow.MaxResolution` | 512 | 1024 | 2048 | 4096 | 4096 |
| `r.Shadow.CSM.MaxCascades` | 1 | 2 | 4 | 4 | 4 |
| `r.Shadow.DistanceScale` | 0.6 | 0.8 | 1.0 | 1.0 | 1.0 |
| `r.Shadow.RadiusThreshold` | 0.06 | 0.04 | 0.03 | 0.01 | 0.01 |
| `r.Shadow.CSM.TransitionScale` | 0 | 0.5 | 1.0 | 1.0 | 1.0 |
| `r.Shadow.PCF.Taps` | 4 | 8 | 16 | 16 | 32 |
| `r.Shadow.ContactShadows` | 0 | 0 | 1 | 1 | 1 |
| `r.DistanceFieldShadowing` | 0 | 0 | 0 | 1 | 1 |

#### 2. Texture Quality (`r.Texture.*`)
| CVar | Low | Medium | High | Ultra | Cinematic |
|------|-----|--------|------|-------|-----------|
| `r.Texture.MaxMipBias` | 2 | 1 | 0 | 0 | -1 |
| `r.Texture.Streaming.MaxAniso` | 2 | 4 | 8 | 16 | 16 |

#### 3. View Distance (`r.ViewDistance.*`)
| CVar | Low | Medium | High | Ultra | Cinematic |
|------|-----|--------|------|-------|-----------|
| `r.ViewDistanceScale` | 0.4 | 0.6 | 0.8 | 1.0 | 1.4 |
| `r.LOD.DistanceScale` | 0.6 | 0.8 | 1.0 | 1.0 | 1.0 |

#### 4. Post-Process Quality (`r.PostProcess.*`)
| CVar | Low | Medium | High | Ultra | Cinematic |
|------|-----|--------|------|-------|-----------|
| `r.TAA` | 0 | 1 | 1 | 1 | 1 |
| `r.Bloom` | 0 | 1 | 1 | 1 | 1 |
| `r.Bloom.Quality` | 0 | 1 | 2 | 3 | 4 |
| `r.GTAO` | 0 | 0 | 1 | 1 | 1 |
| `r.GTAO.Quality` | 0 | 0 | 1 | 2 | 3 |
| `r.SSR` | 0 | 0 | 0 | 1 | 1 |
| `r.MotionBlur` | 0 | 0 | 1 | 1 | 1 |
| `r.DOF` | 0 | 0 | 0 | 1 | 1 |
| `r.LensFlare` | 0 | 0 | 1 | 1 | 1 |

#### 5. Effects Quality (`r.Effects.*`)
| CVar | Low | Medium | High | Ultra | Cinematic |
|------|-----|--------|------|-------|-----------|
| `r.VFX.MaxParticles` | 4096 | 16384 | 65536 | 65536 | 262144 |
| `r.VolumetricClouds` | 0 | 0 | 1 | 1 | 1 |
| `r.Cloud.PrimarySteps` | 16 | 24 | 48 | 64 | 96 |
| `r.HeightFog` | 1 | 1 | 1 | 1 | 1 |

#### 6. Global Illumination Quality (`r.GI.*`)
| CVar | Low | Medium | High | Ultra | Cinematic |
|------|-----|--------|------|-------|-----------|
| `r.SSGI` | 0 | 0 | 0 | 1 | 1 |
| `r.RadianceCache` | 0 | 0 | 0 | 1 | 1 |
| `r.SkyLight.SH` | 1 | 1 | 1 | 1 | 1 |

#### 7. Resolution / Upscaling (`r.Screen.*`)
| CVar | Low | Medium | High | Ultra | Cinematic |
|------|-----|--------|------|-------|-----------|
| `r.ScreenPercentage` | 50 | 67 | 100 | 100 | 100 |
| `r.Upscaler` | TAA | TAA | TAA | TAA | TSR |

## Implementation

### C++ Architecture
```cpp
enum class QualityLevel : uint8_t { Low=0, Medium=1, High=2, Ultra=3, Cinematic=4 };

struct ScalabilityGroup {
    const char* name;
    struct CvarOverride { const char* cvar; float values[5]; };
    std::vector<CvarOverride> overrides;
};

class ScalabilityManager {
    void SetQualityLevel(QualityLevel level);       // Apply all groups
    void SetGroupQuality(const char* group, int l);  // Per-group
    QualityLevel GetOverallQuality() const;
    void LoadFromConfig(const char* path);           // .uzquality INI
};
```

### Config File Format (`.uzquality`)
```ini
[ShadowQuality@0]
r.Shadow.MaxResolution=512
r.Shadow.CSM.MaxCascades=1

[ShadowQuality@3]
r.Shadow.MaxResolution=4096
r.Shadow.CSM.MaxCascades=4
```

## References
- [UE5 Scalability Reference](https://docs.unrealengine.com/4.26/en-US/TestingAndOptimization/PerformanceAndProfiling/Scalability/ScalabilityReference)
- [UE5 Scalability.ini](https://github.com/akshaybabloo/UnrealEngine-and-CPP/blob/master/Mac/UnrealEngine/MyProject/Intermediate/Config/CoalescedSourceConfigs/Scalability.ini)
- [sg.ShadowQuality CVar](https://indxzero.github.io/ue544cvarwiki/articles/sg.shadowquality/)
