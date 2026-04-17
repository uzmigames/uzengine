# 07 — ECS Component Defaults + Animation System

**Severity:** MEDIUM
**File focus:** `runtime/engine/ecs/` components, `runtime/engine/animation/` (newly added), `runtime/shaders/runtime/compute/gpu_skinning_cs.hlsl`

The newly-added animation system (skeletal mesh + GPU skinning) contains several hardcodes that should be flagged before they propagate. Existing ECS components also have sample-calibrated defaults.

## Findings

| # | Category | File:Line | Snippet | Why Bad | Fix |
|---|----------|-----------|---------|---------|-----|
| 7.1 | Max bones | [skeleton.h:36](../../../runtime/engine/animation/skeleton.h#L36) | `static constexpr uint32_t kMaxBones = 256;` | AAA character cap; crowds/quadrupeds blocked | `SkeletonConfig` with tier |
| 7.2 | Default sample rate | [animation_clip.h:112](../../../runtime/engine/animation/animation_clip.h#L112) | `float sampleRate = 30.0f;` | 30fps is arbitrary; FBX may provide 24/60 native | Use clip's native rate; no invented default |
| 7.3 | GPU skinning group size | [gpu_skinning_cs.hlsl:45](../../../runtime/shaders/runtime/compute/gpu_skinning_cs.hlsl#L45) | `[numthreads(64, 1, 1)]` | No justification; GPU occupancy unverified | Cite GPU wave math or benchmark |
| 7.4 | GPU skinning max influences | [gpu_skinning_cs.hlsl:56-60](../../../runtime/shaders/runtime/compute/gpu_skinning_cs.hlsl#L56-L60) | `idx0..idx3, w0..w3` | 4-bone fixed; some rigs need 8; simple rigs waste bandwidth | Compile-time permutation or dynamic |
| 7.5 | Max LOD levels | [lod_group.h:31](../../../runtime/engine/renderer/mesh/lod_group.h#L31) | `static constexpr uint32_t kMaxLODs = 8;` | See 02-capacity-limits.md; static array blocks scaling | `std::vector<LODEntry>` |
| 7.6 | LOD stack reserve | [scene_component.cpp:139](../../../runtime/engine/ecs/scene_component.cpp#L139) | `stack.reserve(16);` | Assumes hierarchy ≤ 16 deep; realloc on overflow | `stack.reserve(max(16, EstimateDepth()))` |
| 7.7 | Camera FOV default | [camera_component.h:48](../../../runtime/engine/ecs/camera_component.h#L48) | `float fov = 60.f;` | Arbitrary default; sample may use 45° / 75° | Document as canonical or load from `.uzproj` |
| 7.8 | Near/far planes | [camera_component.h:53,58](../../../runtime/engine/ecs/camera_component.h#L53-L58) | `nearPlane = 10.f, farPlane = 100000.f` | 10cm–1km range may not suit all scenes | Per-project override; warn on outliers |
| 7.9 | Aspect ratio | [camera_component.h:63](../../../runtime/engine/ecs/camera_component.h#L63) | `aspectRatio = 16.f / 9.f` | Ultrawide / VR / 4:3 unsupported without override | Derive from viewport |
| 7.10 | Scene CVar overrides | [scene_loader.cpp:67](../../../runtime/engine/ecs/loaders/scene_loader.cpp#L67) | Static `s_sceneCVarOverrides` map | No inheritance / rollback; globals leak between scenes | Scoped CVar stack (push/pop) |
| 7.11 | Initial clip name | [skeletal_mesh_component.h:46-48](../../../runtime/engine/ecs/mesh/skeletal_mesh_component.h#L46-L48) | `std::string initialClipName;` + silent fallback | Missing clip → bind pose with warning; no fallback list | Fallback clip list; explicit behavior enum |
| 7.12 | Pose blend truncation | [pose.cpp](../../../runtime/engine/animation/pose.cpp) + [pose.h:104](../../../runtime/engine/animation/pose.h#L104) | `Resize` uses `min(a.size, b.size)` silently | Different bone counts truncate; no warning | Assert / log + explicit downscale API |
| 7.13 | VFX spawn velocity | [vfx_emitter_component.h:51](../../../runtime/engine/ecs/vfx/vfx_emitter_component.h#L51) | `spawnVelocity = {0, 0, 200.0f}` | Z-up hardcoded; 200cm/s upward arbitrary | Inherit emitter transform; world-space compute |
| 7.14 | Reflection probe blend | [reflection_probe_component.h:61](../../../runtime/engine/ecs/reflection_probe_component.h#L61) | `blendDistance = 100.0f` (1m) | Large outdoor needs 10m+; small interior 10cm | Ratio of influence radius (0.1–1.0) |

## Animation System Audit (New Files)

The freshly-added animation files are now reviewed. Confirmed OK:

- **Coordinate system** — respects Z-up LH convention (no Y-up leakage from glTF)
- **Unit scale** — bindpose translations are cm-consistent
- **Entry point** — `gpu_skinning_cs.hlsl` uses `main` (rule compliant)

Issues flagged above (7.1–7.4) relate to capacity and parameterization, not correctness.

## Proposed Component Config Pattern

Instead of inline `constexpr` defaults in component headers, use a `DefaultsRegistry`:

```cpp
// engine/ecs/defaults.h
struct CameraDefaults {
    float fov         = 60.0f;
    float nearPlane   = 10.0f;
    float farPlane    = 100000.0f;
    float aspectRatio = 16.0f / 9.0f;
};

struct SkeletonDefaults {
    uint32_t maxBones = 256;
};

struct AnimationDefaults {
    float  sampleRate    = 30.0f;   // fallback only
    size_t maxInfluences = 4;
};

// Loaded from .uzproj at ProjectConfig::Init()
const DefaultsRegistry& Defaults();
```

Components read `Defaults().camera.fov` in their constructor. Projects override in `.uzproj`:

```json
{
  "ecs": {
    "defaults": {
      "camera": { "fov": 75.0, "aspectRatio": 2.35 },
      "skeleton": { "maxBones": 512 },
      "animation": { "maxInfluences": 8 }
    }
  }
}
```

## Scene Loader — Scoped CVar Overrides

Current: scene-provided CVar overrides write to global map, never rolled back.

### Fix
```cpp
class SceneLoader {
    struct CVarSnapshot { std::string name; Variant value; };
    std::vector<CVarSnapshot> m_sceneSnapshots;

    void OnSceneLoad(Scene& s) {
        for (const auto& [name, value] : s.cvarOverrides) {
            m_sceneSnapshots.push_back({name, CVar::Get(name)});  // save
            CVar::Set(name, value);
        }
    }

    void OnSceneUnload() {
        for (auto it = m_sceneSnapshots.rbegin(); it != m_sceneSnapshots.rend(); ++it) {
            CVar::Set(it->name, it->value);  // restore in reverse order
        }
        m_sceneSnapshots.clear();
    }
};
```

This ensures scene-level overrides don't pollute the global CVar state after unload.

## Animation I/O Header Size

Finding worth tracking (not listed above — format-stability concern):

- [animation_io.h:60](../../../runtime/engine/animation/animation_io.h#L60) — `kUzAnimHeaderSize = 32`
- [skeleton_io.h:56](../../../runtime/engine/animation/skeleton_io.h#L56) — `kUzSkelHeaderSize = 16`

These hardcoded header sizes block format evolution. Add a version field and document a migration plan:

```cpp
struct UzAnimHeader {
    uint32_t magic;      // 'UZAN'
    uint32_t version;    // current = 2
    uint32_t headerSize; // sizeof(UzAnimHeader) — self-describing
    uint32_t flags;
    // ... rest follows version-dependent layout
};
```
