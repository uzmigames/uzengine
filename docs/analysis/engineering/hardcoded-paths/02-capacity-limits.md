# 02 — Fixed Compile-Time Capacity Limits

**Severity:** HIGH
**File focus:** Compile-time `constexpr` caps across renderer, ECS, animation, RHI

Fixed limits prevent the engine from scaling to projects larger than the sample content. They also impose hidden failure modes — exceeding the cap either asserts or silently drops work.

## Findings

| # | Category | File:Line | Snippet | Why Bad | Fix |
|---|----------|-----------|---------|---------|-----|
| 2.1 | Instance batching cap | [instance_batcher.h:48](../../../runtime/engine/renderer/core/instance_batcher.h#L48) | `static constexpr uint32_t kMaxInstances = 16384;` | Large scenes (crowds, foliage, GPU-driven) exceed 16k | Use growable buffer or `.uzproj` config |
| 2.2 | Radiance cache probes | [radiance_cache.h:55,208](../../../runtime/engine/renderer/gi/radiance_cache.h#L55) | `kTotalProbes = 4096 (16³ grid)` | Grid fixed regardless of scene extent | Scale grid to scene AABB; allow sparse layout |
| 2.3 | Irradiance probe grid max | [irradiance_probe_grid.h:70](../../../runtime/engine/renderer/gi/irradiance_probe_grid.h#L70) | `static constexpr uint32_t kMaxProbes = 16384;` | Prevents larger GI volumes | CVar or `.uzproj`; dynamic buffer |
| 2.4 | VFX particle death events | [frame_vfx_inline.cpp:125,128](../../../runtime/engine/renderer/frame/frame_vfx_inline.cpp#L125) | `4096 events × 32 bytes` | Dense particle scenes overflow | Dynamic sizing per emitter count |
| 2.5 | RT acceleration instances | [rt_manager.cpp:19,53](../../../runtime/engine/renderer/raytracing/rt_manager.cpp#L53) | Hardcoded `4096` check at line 53 shadows CVar | CVar `r.RayTracing.MaxInstances` is ignored by hardcoded check | Use CVar value dynamically |
| 2.6 | Max bones per skeleton | [skeleton.h:36](../../../runtime/engine/animation/skeleton.h#L36) | `static constexpr uint32_t kMaxBones = 256;` | Multi-character crowds capped; high-detail rigs rejected | `SkeletonConfig` with budget tiers |
| 2.7 | Max LOD levels | [lod_group.h:31](../../../runtime/engine/renderer/mesh/lod_group.h#L31) | `static constexpr uint32_t kMaxLODs = 8;` | Copied from reference without justification; static array blocks scaling | `std::vector<LODEntry>` or raise to 16 |
| 2.8 | VFX max particles default | [vfx_emitter_component.h:47](../../../runtime/engine/ecs/vfx/vfx_emitter_component.h#L47) | `uint32_t maxParticles = 10000;` | 640KB+/emitter memory; no scene budget check | Global `kMaxParticlesPerScene` CVar + warn on overrun |
| 2.9 | DX12 CBV/SRV/UAV heap | [dx12_device.h:47](../../../runtime/engine/renderer/rhi/dx12_device.h#L47) | `kMaxCbvSrvUavDescriptors = 1'000'000` | 1M bindless descriptors fixed; no scalability tier | Make configurable in `RHIDeviceDesc`; validate against hardware |
| 2.10 | DX12 sampler heap | [dx12_device.h:50](../../../runtime/engine/renderer/rhi/dx12_device.h#L50) | `kMaxSamplerDescriptors = 2048` | DX12 caps sampler heap at 2048; at-limit without capability query | Query device; halve if hardware reports less |
| 2.11 | DX12 RTV/DSV heaps | [dx12_device.h:53-56](../../../runtime/engine/renderer/rhi/dx12_device.h#L53-L56) | `kMaxRtvDescriptors = 256; kMaxDsvDescriptors = 64` | Complex frame graphs can exceed 256 RTVs (MRT + shadow + GI + VFX) | Query device limits; use as defaults |

## Patterns

### Anti-pattern: compile-time `constexpr` for content-scaling caps
```cpp
// BAD: caller cannot override for large projects
static constexpr uint32_t kMaxInstances = 16384;
InstanceData g_instances[kMaxInstances];
```

### Preferred: runtime-configurable with sensible default
```cpp
// GOOD: reads from ProjectConfig, growable
class InstanceBatcher {
  std::vector<InstanceData> m_instances;
  uint32_t m_budget;  // from ProjectConfig::Rendering::maxInstances, default 16384
  
  void Reserve(uint32_t estimated) {
    m_instances.reserve(std::min(estimated, m_budget));
  }
};
```

## Capacity Budget Table (proposed `.uzproj` schema)

```json
{
  "rendering": {
    "capacity": {
      "maxInstances": 16384,
      "maxSkeletalMeshes": 512,
      "maxProbes": 16384,
      "maxParticlesPerScene": 1000000,
      "maxShadowCasters": 64
    },
    "animation": {
      "maxBonesPerSkeleton": 256,
      "maxLODLevels": 8
    }
  },
  "rhi": {
    "descriptorHeaps": {
      "cbvSrvUav": 1000000,
      "sampler": 2048,
      "rtv": 256,
      "dsv": 64
    }
  }
}
```

## Failure Mode Audit

For each cap, confirm the behavior when exceeded:

| Cap | Current behavior | Should be |
|-----|------------------|-----------|
| `kMaxInstances` | Unknown — likely silent drop | Grow buffer, warn on first overflow |
| `kMaxBones` | `AddBone` returns `false` silently | Return `Result<>` with error |
| `kMaxLODs` | Static array index OOB | `vector` growth |
| `kMaxProbes` | Unknown | Warn + clamp |
| `kMaxCbvSrvUavDescriptors` | Heap allocation may fail | Query hardware pre-init |

## Priority Order

1. **`kMaxInstances`** — most likely to hit; large scenes fail first
2. **`kMaxBones`** — blocks multi-character pipelines
3. **RHI descriptor heaps** — platform-dependent; query hardware
4. **`kMaxProbes` / `kTotalProbes`** — GI volumes don't scale
5. **VFX buffers** — affect dense particle scenes
