<!-- doc-meta -->
# Niagara GPU Particle Buffer Initialization Patterns

**Source**: UE5.6 Niagara Plugin (W:\UE_5.6_Source\Engine\Plugins\FX\Niagara)

## Key Findings

### 1. Free List Initialization Strategy

**Pattern**: Two-phase compute shader initialization

- **NiagaraInitFreeIDBuffer.usf** (compute shader):
  - Called ONCE when buffer is allocated or resized
  - Merges new IDs (indices 0 to NumNewElements-1) with existing IDs
  - New IDs value = NumExistingElements + Index
  - Existing IDs copied after new IDs
  - Uses **CopyBuffer approach** internally via shader dispatch (not direct CopyBuffer RHI call)

- **NiagaraComputeFreeIDs.usf** (compute shader, per-frame):
  - Runs every simulation frame after spawn shader
  - IDToIndexTable stores -1 for dead particles, actual indices for alive ones
  - Wave-intrinsics path (PS5/XB1): WaveActiveCountBits + WavePrefixCountBits for fast prefix sum
  - Non-intrinsics path: Group shared memory with binary tree prefix sum
  - InterlockedAdd to maintain RWFreeIDListSizes[FreeIDListIndex]
  - Outputs compact free ID list to RWFreeIDList

### 2. First-Frame Initialization

**Pattern**: Simulator expects zeroed buffers; no special "init" pass required

- **AllocateGPU()** (NiagaraDataSet.cpp:826):
  - Particle buffers allocated with no explicit ClearUAV
  - Float/Int/Half buffers initialized to **ERHIAccess::SRVMask** (CPU-readable, no GPU init)
  - IDToIndexTable also allocated, no pre-fill (implicitly zero)

- **Spawn shader behavior**:
  - Reads IDToIndexTable (all zeros on first frame = all dead)
  - ComputeFreeIDs shader fills free ID list during first dispatch
  - First spawn tick consumes free IDs; new particles written to simulation buffer
  - **Key**: Simulator designed to work with zeroed buffers; no special "init" compute pass needed

- **Instance count initialization** (NiagaraGPUInstanceCountManager.cpp:104-105):
  ```cpp
  const ERHIAccess kCountBufferDefaultState = ERHIAccess::SRVMask | ERHIAccess::CopySrc;
  const ERHIAccess kIndirectArgsDefaultState = ERHIAccess::IndirectArgs | ERHIAccess::SRVMask;
  ```
  - Instance counts start at SRV state (readable by CPU/other shaders)
  - No zero-fill needed; buffer managed by allocator

### 3. Resource State Management (Between Frames)

**Pattern**: Explicit ERHIAccess transitions; no implicit state retention

| Buffer | Default State | During Simulation | After Simulation |
|--------|---------------|-------------------|-----------------|
| Particle Float/Int/Half | SRVMask | UAVCompute | SRVMask |
| IDToIndexTable | SRVMask | UAVCompute (write) | SRVCompute (read) |
| Free ID List | (internal) | UAVCompute (write) | SRVCompute (read) |
| Free ID List Sizes | UAVCompute | UAVCompute (atomic) | UAVCompute |
| Instance Count | SRVMask \| CopySrc | UAVCompute | SRVMask \| CopySrc |
| Indirect Args | IndirectArgs \| SRVMask | (read-only) | IndirectArgs \| SRVMask |

**Transition logic** (NiagaraGpuComputeDispatch.cpp:1190-1304):
- **PreStageTransitions**: Buffers transitioned to UAVCompute before dispatch
- **PostStageTransitions**: Buffers transitioned back to SRVCompute/SRVMask after dispatch
- Transitions happen at RDG pass level, not inside shader dispatch
- FreeIDUpdate buffers transition: SRVCompute → UAVCompute (cleared then recomputed each frame)

### 4. Free Index Pool Management

**NiagaraGPUInstanceCountManager**:
- Maintains global pools for indirect args, instance counts
- **InstanceCountClearTasks**: Deferred clear list (freed entries marked for CPU readback)
- No pre-allocated pool for particle IDs; free IDs recomputed from IDToIndexTable each frame
- Free ID list is **ephemeral** — rebuilt every frame from scratch

**Allocation pattern**:
```
Spawn shader → InterlockedAdd(FreeIDListSizes) → allocates N particles
ComputeFreeIDs → Rebuilds free ID list based on IDToIndexTable == -1
Next frame → Repeat
```

## UzEngine Adaptation

1. **Compute shader, not ClearUAV**: Initialize free list via compute (not RHI ClearUAV)
2. **IDToIndexTable per-frame**: Maintain -1 for dead, alive index for spawned particles
3. **Wave intrinsics**: Use HLSL wave intrinsics where available (DX12 SM6+) for efficient prefix sum
4. **Zeroed buffer expectation**: Simulator designed for zeroed allocation; no pre-initialization pass
5. **Explicit transitions**: Manage ERHIAccess (SRVMask ↔ UAVCompute) at render graph level

## Key Code Locations

- `NiagaraInitFreeIDBuffer.usf`: One-time free list merge on allocation
- `NiagaraComputeFreeIDs.usf`: Per-frame free ID reconstruction (wave or LDS-based prefix sum)
- `NiagaraClearCounts.usf`: Generic count clear dispatch (used for indirect args, instance counts)
- `NiagaraDataSet.cpp::AllocateGPU()`: Buffer lifecycle and state initialization
- `NiagaraGpuComputeDispatch.cpp::ExecuteTicks()`: Transition orchestration, dispatch scheduling

**Line references**:
- AllocateGPUFreeIDs: NiagaraDataSet.cpp:452
- AllocateGPU buffer init: NiagaraDataSet.cpp:826-890
- State transitions: NiagaraGpuComputeDispatch.cpp:1190-1304
- Instance count default states: NiagaraGPUInstanceCountManager.cpp:104-105
