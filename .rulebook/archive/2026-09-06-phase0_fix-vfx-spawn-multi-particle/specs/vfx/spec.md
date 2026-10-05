# VFX GPU Particle Spawn — Buffer Lifecycle and State Management

## ADDED Requirements

### Requirement: Deferred Staging Buffer Destruction
GPU staging buffers used by InitializeData for CopyBuffer commands SHALL NOT be destroyed until at least kFramesInFlight (3) frames have passed since the CopyBuffer was recorded on the command list. D3D12MA releases heap memory immediately on Allocation::Release(), which corrupts in-flight CopyBuffer data under triple-buffered frame pacing.

#### Scenario: Staging buffers survive until GPU completes
Given InitializeData records CopyBuffer commands on frame 0
When frame 1 begins recording before frame 0's GPU work completes
Then the staging buffers (freeListStaging, freeListHeadStaging, zeroStagingBuffer) MUST remain alive
And they SHALL only be destroyed when frameIndex >= initFrameIndex + kFramesInFlight

#### Scenario: No staging buffer leak at shutdown
Given staging buffers were deferred for destruction
When the application shuts down before the deferred frame count is reached
Then DestroyStaging() MUST destroy all remaining staging buffers

### Requirement: First-Frame WriteSet Common to UAV Transition
On the first simulation frame, WriteSet attribute buffers that were never explicitly transitioned (positions, velocities, sizes, colors, rotations) SHALL be transitioned from Common to UnorderedAccess before the spawn compute shader dispatch.

#### Scenario: WriteSet positions buffer first-frame barrier
Given the VFX system runs its first simulation frame (hasCompletedCycle == false)
When the WriteSet positions buffer is in Common state (freshly created, never CopyBuffer'd)
Then a Common→UnorderedAccess barrier MUST be emitted before the spawn CS dispatch

### Requirement: GPU Readback Validation for InitializeData
A one-time GPU readback of freeListHead SHALL be performed after InitializeData to verify the CopyBuffer correctly delivered the initial value (capacity). This validation MUST log the result and assert correctness.

#### Scenario: freeListHead validation passes
Given InitializeData copies capacity (10000) to freeListHead
When the GPU readback completes
Then the read value MUST equal capacity
And an info log SHALL confirm the validation passed
