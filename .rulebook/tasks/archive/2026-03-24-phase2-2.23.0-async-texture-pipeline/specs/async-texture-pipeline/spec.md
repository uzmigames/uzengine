# Async Texture Pipeline — Spec Delta

## ADDED Requirements

### Requirement: sRGB-Aware Texture Format Selection

The texture loading system MUST create GPU textures with the correct color space format. Textures representing color data (albedo, emissive, detail albedo) MUST be created with `RGBA8Srgb` (or `BC7_SRGB` when compressed). Textures representing linear data (normal, roughness, metallic, AO, ORM, heightmap, opacity, thickness) MUST be created with `RGBA8Unorm` (or `BC7_UNORM`). The format MUST be determined by the texture slot type and optionally overridden per-texture in `.uzmat`.

#### Scenario: Albedo texture created as sRGB
Given a PNG texture assigned to the "Albedo" slot of a material
When `TextureLoader` creates the GPU texture
Then the texture format SHALL be `RHIFormat::RGBA8Srgb`
And the GPU hardware SHALL perform automatic sRGB-to-linear conversion on sample
And the shader SHALL receive linear-space values without manual `pow(color, 2.2)` conversion

#### Scenario: ORM texture created as linear
Given a PNG texture assigned to the "ORM" slot of a material (packed AO+Roughness+Metallic)
When `TextureLoader` creates the GPU texture
Then the texture format SHALL be `RHIFormat::RGBA8Unorm`
And the sampled values SHALL be raw linear [0,1] without gamma conversion
And roughness/metallic/AO values SHALL NOT be double-gamma-corrected

#### Scenario: Artist overrides sRGB in .uzmat
Given a `.uzmat` file with `"textures": { "roughness": { "path": "custom.png", "srgb": true } }`
When the texture is loaded
Then the texture format SHALL be `RHIFormat::RGBA8Srgb` regardless of the default for that slot
And a warning SHALL be logged that roughness slot is typically linear

### Requirement: Zero Synchronous Texture Load on Main Thread

All texture loading paths (dev mode loose files, PAK/VFS compressed, `.uztex` pre-compressed) MUST route through `AsyncTextureDecoder` for CPU-side work. The main thread SHALL NOT call `stbi_load`, `stbi_load_from_memory`, or CPU mip generation directly. The only main-thread work allowed is GPU upload command recording and fence management.

#### Scenario: PAK mode texture load is non-blocking
Given a scene with 47 textures stored in a PAK file
When `SyncWorldToDrawList()` triggers material texture loading
Then all 47 textures SHALL be submitted to `AsyncTextureDecoder` without blocking
And the main thread frame time SHALL not exceed 5ms for the submission
And the scene SHALL render with placeholder textures on the next frame

#### Scenario: Dev mode loose file load remains non-blocking
Given a scene with 47 loose PNG/JPG files on disk
When `SyncWorldToDrawList()` triggers material texture loading
Then all 47 textures SHALL be submitted to `AsyncTextureDecoder`
And the behavior SHALL be identical to the PAK mode path

### Requirement: Batched GPU Upload with Single Fence

Texture GPU uploads MUST be batched into a single command list submission per frame. The system SHALL NOT call `WaitIdle()` per individual texture. A single fence wait per batch is permitted.

#### Scenario: 47 textures uploaded in one GPU batch
Given 47 textures have completed async CPU decode
When `FlushPendingLoads()` is called
Then all 47 textures SHALL be uploaded via a single `Execute()` call
And `WaitIdle()` or fence wait SHALL be called exactly once for the batch
And total GPU stall time SHALL be less than 30ms for the batch

#### Scenario: Partial batch upload when decode is in progress
Given 20 of 47 textures have completed async CPU decode and 27 are still pending
When `FlushPendingLoads()` is called
Then the 20 completed textures SHALL be uploaded in a single batch
And the remaining 27 SHALL continue decoding without stall
And subsequent frames SHALL upload remaining textures as they complete

### Requirement: .uztex Cook-Time Compressed Format

The `uzcook` tool MUST support converting PNG/JPG textures to `.uztex` format containing GPU-compressed data (BC1/BC3/BC4/BC5/BC7) with pre-baked mip chain. Runtime SHALL load `.uztex` files with zero CPU decode overhead.

#### Scenario: PNG albedo texture cooked to BC7
Given a 2048×2048 sRGB PNG albedo texture
When `uzcook --textures` processes it
Then the output `.uztex` file SHALL contain BC7-compressed data for all mip levels (2048 down to 1×1)
And the `.uztex` header SHALL specify format=BC7, flags=sRGB
And the file size SHALL be approximately 5.3MB (2048² × 1 byte/texel for BC7 + mips)

#### Scenario: Normal map cooked to BC5
Given a 2048×2048 tangent-space normal map PNG
When `uzcook --textures` processes it
When the texture is referenced with slot name "Normal" in a `.uzmat`
Then the output `.uztex` SHALL use BC5 (RG) format for optimal two-channel quality
And the header SHALL specify flags=normalMap (not sRGB)

#### Scenario: Runtime loads .uztex without CPU decode
Given a `.uztex` file with BC7-compressed data
When `TextureLoader::LoadUzTex()` reads it
Then `stbi_load` SHALL NOT be called
And CPU mip generation SHALL NOT run
And the compressed blocks SHALL be uploaded directly to GPU via `CopyBufferToTexture()`
And the GPU texture format SHALL match the `.uztex` header format

### Requirement: Placeholder Mip System

The texture loading system MUST support instant scene visibility by loading a small placeholder mip (mip 0 = smallest resolution) immediately and streaming higher-resolution mips asynchronously.

#### Scenario: Scene visible with placeholders in first frame
Given a scene with 47 textures, each stored as `.uztex` with embedded mip 0
When the scene is loaded
Then all 47 textures SHALL have valid GPU SRV indices by the end of frame 1
And each SRV SHALL point to a texture containing at least mip 0 (16×16 or 32×32)
And the scene SHALL be rendered with blurry but correctly colored surfaces

#### Scenario: Progressive mip sharpening
Given a texture in Placeholder state with mip 0 loaded
When higher mips (1, 2, ..., N) finish async loading
Then the texture SRV SHALL be swapped to a new texture containing all loaded mips
And the swap SHALL occur between frames (not mid-frame)
And draw commands SHALL automatically use the updated SRV on the next frame

### Requirement: Copy Queue Non-Blocking Upload

The RHI MUST provide a dedicated copy queue for texture uploads that does not stall the graphics queue. Texture uploads SHALL execute on the copy queue while rendering continues on the graphics queue.

#### Scenario: Texture upload concurrent with rendering
Given the graphics queue is executing draw calls for frame N
When 10 textures complete CPU decode and need GPU upload
Then the uploads SHALL be recorded on the copy queue command list
And the graphics queue SHALL not be stalled or paused
And the uploaded textures SHALL become available for frame N+1 (after copy fence signals)

#### Scenario: Staging buffer double-buffering
Given frame N is uploading textures via staging buffer A on the copy queue
When frame N+1 needs to upload more textures
Then staging buffer B SHALL be used for frame N+1 uploads
And staging buffer A SHALL not be reused until its copy fence signals completion

### Requirement: Mip Streaming Manager

The system MUST provide a per-frame mip streaming loop that calculates required mip levels per texture based on screen coverage and manages async loading of mip data within a configurable VRAM budget.

#### Scenario: Camera approaches a textured surface
Given a texture currently loaded at mip 3 (256×256) with full resolution being mip 0 (2048×2048)
When the camera moves close enough that screen coverage requires mip 1 (1024×1024)
Then the streaming manager SHALL queue async load for mips 1-2
And mips SHALL be loaded in order from lowest to highest resolution
And the texture SRV SHALL be updated as each mip level becomes available

#### Scenario: VRAM budget exceeded
Given total texture VRAM usage is 1100MB and the budget is 1024MB
When the streaming manager runs its per-frame update
Then textures with lowest screen coverage SHALL be downgraded to lower mip levels
And downgraded textures SHALL be evicted using LRU ordering
And VRAM usage SHALL be brought below the budget within 5 frames

#### Scenario: Streaming priority for visible textures
Given texture A is visible at center screen requiring mip 0
And texture B is at the edge of view requiring mip 2
And texture C is off-screen
When the streaming manager processes requests
Then texture A SHALL have highest priority
And texture C SHALL have lowest priority (may be deferred or evicted)

### Requirement: Async Texture Decoder Worker Scaling

The `AsyncTextureDecoder` MUST scale worker thread count based on available CPU cores. Default SHALL be `max(2, hardware_concurrency / 2)`, clamped to [1, 8].

#### Scenario: 8-core CPU scales to 4 workers
Given a system with 8 logical CPU cores
When `AsyncTextureDecoder` is initialized with default settings
Then 4 worker threads SHALL be created
And all 4 workers SHALL process texture decode jobs concurrently

## MODIFIED Requirements

### Requirement: scene_assets.cpp LoadTexture PAK Branch (MODIFIED)

The PAK/VFS branch of `LoadTexture()` in `scene_assets.cpp` SHALL route through `AsyncTextureDecoder` instead of calling `TextureLoader::LoadFromMemory()` synchronously. VFS decompression (LZ4) SHALL happen on the calling thread (fast, <1ms), then decoded bytes submitted to async decoder for stbi_load + mip gen on worker thread.

#### Scenario: PAK texture load path matches dev mode behavior
Given a texture path that resolves to a PAK/VFS file
When `LoadTexture()` is called
Then VFS SHALL decompress the data (LZ4) on the calling thread
And the decompressed bytes SHALL be submitted to `AsyncTextureDecoder::SubmitFromMemory()`
And the function SHALL return immediately with a placeholder SRV index
And the full texture SHALL be uploaded asynchronously when decode completes

### Requirement: TextureLoader WaitIdle Removal (MODIFIED)

The `TextureLoader::UploadPixels()` function SHALL NOT call `device->WaitIdle()`. All upload paths SHALL use deferred recording via `UploadPixelsDeferred()` or `UploadPixelsWithMipsDeferred()`. The caller is responsible for batching and flushing.

#### Scenario: UploadPixels no longer stalls GPU
Given code calls `TextureLoader::UploadPixels()`
When the upload completes
Then `device->WaitIdle()` SHALL NOT be called inside the function
And the upload commands SHALL be recorded to the caller's command list
And the caller SHALL flush the command list at their discretion
