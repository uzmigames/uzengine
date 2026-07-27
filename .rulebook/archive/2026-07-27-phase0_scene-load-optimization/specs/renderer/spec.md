# Spec: Scene load performance (renderer)

## ADDED Requirements

### Requirement: Asynchronous mesh loading
The renderer SHALL load `.uzmesh` files (file read + parse) on background worker
threads, keeping the main thread free of per-mesh file I/O. GPU upload SHALL remain
batched on the main thread. Draw commands for meshes whose data is not yet resident
SHALL be skipped (existing placeholder-slot deferral), never stalled on.

#### Scenario: Scene with many meshes
Given a scene referencing 138 .uzmesh files
When SyncWorldToDrawList requests the meshes
Then file reads and parsing execute on worker threads in parallel
And the main thread only integrates completed meshes and records batched GPU copies
And every mesh eventually becomes resident with correct geometry

#### Scenario: Mesh load failure
Given a mesh path that does not exist or fails validation
When the async loader completes the request
Then the slot logs an error and remains an invalid placeholder
And rendering continues without crashing

### Requirement: Mesh upload flush without device stall
`FlushDeferredMeshUploads` MUST NOT call a full-device `WaitIdle()`. Staging buffers
SHALL be released only after the GPU has consumed them, via the fence-guarded
deferred-destroy queue. Copies and required barriers SHALL be recorded in the frame
command list so same-frame draws observe completed vertex/index data.

#### Scenario: Frame that loads meshes
Given a frame in which N meshes finished async loading
When FlushDeferredMeshUploads runs
Then no WaitIdle occurs on the device
And the meshes' vertex/index buffers are usable by draws recorded later in that frame

### Requirement: Material PSO bytecode disk cache
The material pipeline SHALL persist compiled shader bytecode keyed by material graph
content hash and shader model. On a warm run, a cache hit SHALL skip the DXC compile.
A stale or corrupt cache entry SHALL fall back to a fresh compile and overwrite the
entry. The cache location MUST be resolved without hardcoded project paths.

#### Scenario: Warm run
Given Sponza was loaded once and its 26 material PSOs were compiled
When the same scene loads again
Then zero DXC material compiles occur and PSO creation uses cached bytecode

### Requirement: Texture upload budget CVar is honored
The per-frame async texture GPU upload budget SHALL be controlled by the CVar
`r.Scene.MaxTextureUploadsPerFrame` (default 8).

#### Scenario: Override via CLI
Given the engine starts with --cvar r.Scene.MaxTextureUploadsPerFrame=16
When async-decoded textures are flushed
Then up to 16 textures upload per frame

## MODIFIED Requirements

### Requirement: Scene loader material parsing
During a single LoadScene call, each unique `.uzmat` file SHALL be read and parsed at
most once; entities sharing a material SHALL reuse the parsed result. The 3-arg
LoadScene overload SHALL instantiate `"prefab"` entries whenever the caller can supply
a VFS; prefab loss MUST NOT be silent (a warning is logged when no VFS is available).

#### Scenario: Shared materials
Given 138 entities referencing 28 unique .uzmat files
When the scene loads
Then exactly 28 .uzmat reads occur

#### Scenario: Prefab via engine app path
Given a scene containing a prefab entity loaded through the engine application path
When LoadScene executes with a project VFS available
Then the prefab children are instantiated identically to the uzengine.exe path
