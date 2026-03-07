# Spec Delta: Fast Texture Loading Pipeline

## ADDED Requirements

### Requirement: Staging Ring Frame Tracking
Each staging ring block SHALL track the frame number in which it was last used (`frame_used`). A block is safe to recycle when `current_frame - frame_used >= 2` (double-buffered safety). The staging ring SHALL expose a `begin_frame(frame_number)` function called once per frame to advance the frame counter.

#### Scenario: Frame-based block recycling
Given a staging ring block last used in frame F
When the current frame is F+2 or later
Then the block is safe to reuse without GPU synchronization

#### Scenario: Block still in-flight
Given a staging ring block last used in frame F
When the current frame is F+1
Then the block MUST NOT be reused (GPU may still be reading)

### Requirement: Async CPU Texture Decode
Texture decoding (stbi_load) SHALL execute on dedicated worker threads, NOT on the main/render thread. An `AsyncTextureLoader` class SHALL manage a pool of 2 worker threads that process decode requests from a thread-safe queue.

#### Scenario: Submit and poll lifecycle
Given a texture path submitted to AsyncTextureLoader
When the worker thread completes stbi_load
Then poll_completed() returns the decoded pixels, width, height for that path

#### Scenario: Multiple concurrent decodes
Given 10 texture paths submitted simultaneously
When worker threads process them (2 threads)
Then all 10 complete within ~5x the single-texture decode time (parallel speedup)

#### Scenario: Failed decode
Given a path to a nonexistent or corrupt file
When the worker thread attempts stbi_load
Then poll_completed() returns a result with pixels=nullptr (no crash, no hang)

### Requirement: Placeholder Textures
The TextureManager SHALL pre-create three 1x1 placeholder textures at initialization:
- `PLACEHOLDER_WHITE` (RGBA 255,255,255,255) -- used for albedo, metallic, roughness, AO
- `PLACEHOLDER_NORMAL` (RGBA 128,128,255,255) -- flat normal map
- `PLACEHOLDER_BLACK` (RGBA 0,0,0,255) -- used for displacement, emission

#### Scenario: Immediate placeholder binding
Given a material requests texture "brick_albedo.png" not yet loaded
When get_or_queue() is called
Then it returns the PLACEHOLDER_WHITE handle immediately (not RHI_NULL)
And the material can render with the placeholder in the same frame

#### Scenario: Transparent handle swap
Given a placeholder handle H was returned for "brick_albedo.png"
When the async decode completes and GPU upload finishes
Then handle H now points to the real texture data
And all materials referencing H automatically see the real texture (no rebinding)

### Requirement: Batched GPU Upload (existing, preserved)
The RHI SHALL provide `texture_upload_batch()` that accepts an array of upload descriptors and records all copy commands into a single command list submission with one fence signal.

### Requirement: Zero Main-Thread Decode
The `TextureManager::tick()` function SHALL NOT call `stbi_load()` or `decode_texture_from_file()`. All CPU decoding MUST happen on worker threads. `tick()` only polls completed results and submits GPU uploads.

#### Scenario: tick() time budget
Given 50 textures completing decode in one frame
When tick() processes all 50 (create GPU textures + batch upload)
Then tick() completes in < 1ms (no stbi_load, only GPU resource creation + memcpy to staging)

## MODIFIED Requirements

### Requirement: TextureManager get_or_queue() Return Value
Previously returned `RHI_NULL` for textures not yet loaded. NOW returns a valid placeholder RhiTexture handle. This eliminates the need for `refresh_textures()` material re-scanning.

#### Scenario: No more refresh_textures() scan
Given 10 materials with 50 texture slots total
When textures load asynchronously over multiple frames
Then materials automatically see updated textures via the same handle (zero per-frame scanning)

## REMOVED Requirements

### Requirement: refresh_textures() O(N) Material Scan
The `MaterialManager::refresh_textures()` function is no longer needed. Materials hold placeholder handles that are updated in-place. This function MAY be kept as a no-op for backward compatibility but SHOULD be removed.
