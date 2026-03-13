# DemoApplication Guide

> **Target Audience**: Developers writing simple graphics demos and validation tests
> **Scope**: Using `DemoApplication` to eliminate GPU boilerplate (window, device, command list, fences, frame sync)
> **Time**: ~10 minutes to write a minimal demo
> **Source**: `runtime/engine/app/demo_application.h`, `runtime/engine/app/demo_application.cpp`

## Overview

`DemoApplication` is a lightweight base class that handles GPU initialization boilerplate for simple demos. Instead of manually creating windows, RHI devices, swapchains, command lists, and synchronization primitives, subclasses only implement virtual hooks: `OnInit()`, `OnFrame()`, `OnResize()` (optional), and `OnShutdown()` (optional).

**Use DemoApplication when**:
- Writing a simple graphics demo (~200 lines)
- Validating a rendering feature (textures, shaders, pipelines)
- Creating a test that needs a window and GPU device
- You don't need the full EngineLoop subsystem framework
- You want automatic triple-buffering and frame synchronization

**Use EngineLoop (see [migration guide below](#migration-to-engineloop)) when**:
- Building a full game with multiple systems (physics, audio, input, scripting)
- You need ordered subsystem initialization and shutdown
- You want dependency-based initialization across many modules
- You need the time manager and tick groups

---

## What DemoApplication Provides

`DemoApplication` owns and manages the full GPU initialization and frame synchronization lifecycle:

| Component | Managed By | Your Responsibility |
|-----------|-----------|---|
| **Window** | DemoApplication | Subclass only calls hooks, reads size via `GetWidth()` / `GetHeight()` |
| **RHI Device** | DemoApplication | Use via `GetDevice()` in `OnInit()` / `OnFrame()` |
| **Swapchain** | DemoApplication | Access via `GetSwapChain()`, handles resize internally |
| **Command List** | DemoApplication | Opened/closed by base class — record commands in `OnFrame()` |
| **Frame Synchronization** | DemoApplication | Triple-buffering with per-frame fences (3 frames in-flight max) |
| **Resize Handling** | DemoApplication | Detects resize, calls `OnResize()`, updates internal state |
| **Minimize Sleep** | DemoApplication | Skips rendering while minimized, polls messages |
| **Message Pump** | DemoApplication | Handles window events, minimize, resize |
| **Shutdown Stats** | DemoApplication | Logs frames, elapsed time, average FPS on exit |

### Triple-Buffering Frame Sync

`DemoApplication` uses 3 frames in flight (`kFramesInFlight = 3`). Before recording new commands each frame:

1. The base class identifies the oldest frame's fence
2. Waits on that fence (GPU finished with those resources)
3. Records new commands into that frame's command list
4. Submits, presents, and signals the new fence value

This ensures the GPU never stalls waiting for the CPU, and the CPU never overwrites resources the GPU is still using.

### Resize Handling

On window resize (`WasResized()` returns true):

1. `WaitIdle()` is called (stalls until GPU finishes all in-flight frames)
2. Swapchain is resized
3. `OnResize(newWidth, newHeight)` is called (optional hook for subclasses to recreate size-dependent resources like depth buffers)
4. `GetWidth()` and `GetHeight()` return updated dimensions

### Minimize Behavior

While the window is minimized (swapchain surface is 0×0):

- Frame recording is skipped
- The message pump continues (window stays responsive)
- `OnFrame()` is not called while minimized

---

## Virtual Hooks

### OnInit() — One-Time Setup

```cpp
protected:
    virtual void OnInit() = 0;
```

Called once after device, swapchain, and command list creation. Load shaders, textures, buffers, and create pipelines here.

**When called**: After `Run()` initializes the GPU
**What's available**: `GetDevice()`, `GetCommandList()`, `GetSwapChain()`, `GetWidth()`, `GetHeight()`
**Exceptions**: Throwing from `OnInit()` returns error code from `Run()`

**Example**:
```cpp
void MyDemo::OnInit() override {
    // Load shaders
    m_vsHandle = m_compiler->Compile("shader.hlsl", ShaderStage::Vertex);
    m_psHandle = m_compiler->Compile("shader.hlsl", ShaderStage::Pixel);

    // Create GPU resources
    m_texHandle = GetDevice()->CreateTexture({
        .width = 256, .height = 256, .format = RHIFormat::RGBA8_Unorm
    });

    // Create pipeline
    m_psoHandle = GetDevice()->CreatePipeline({
        .vs = m_vsHandle, .ps = m_psHandle
    });
}
```

### OnFrame(dt) — Per-Frame Rendering

```cpp
protected:
    virtual void OnFrame(float dt) = 0;
```

Called every frame. Record rendering commands here.

**When called**: Every frame (skipped if window is minimized)
**Command list state**: Already opened with `Begin()` — just record commands, don't close
**Delta time**: `dt` in seconds, clamped to 0.1s max (prevents explosion after breakpoints)
**What to do**:
- Clear the render target
- Record draw calls
- Transition resources as needed
- Read frame index with `GetFrameIndex()` for per-frame data (e.g., which constant buffer slot to use)

**Example**:
```cpp
void MyDemo::OnFrame(float dt) override {
    // Clear
    GetCommandList()->ClearRenderTarget(m_rtHandle, {0.2f, 0.2f, 0.2f, 1.0f});

    // Draw
    GetCommandList()->SetPipeline(m_psoHandle);
    GetCommandList()->SetBinding(0, m_texHandle, m_samplerHandle);
    GetCommandList()->SetRootConstant(1, GetFrameIndex());  // Frame-specific data
    GetCommandList()->Draw(6, 0);  // 2 triangles
}
```

### OnResize(width, height) — Optional Resize Hook

```cpp
protected:
    virtual void OnResize(uint32_t width, uint32_t height) {
        (void)width; (void)height;  // Default: do nothing
    }
```

Called when the window is resized. Recreate size-dependent resources (depth buffers, render targets) here.

**When called**: After `WaitIdle()` and swapchain resize, before next frame
**New dimensions**: `GetWidth()` and `GetHeight()` return updated values
**Default**: Empty (no override needed if you don't have size-dependent resources)

**Example**:
```cpp
void MyDemo::OnResize(uint32_t width, uint32_t height) override {
    // Recreate depth buffer with new size
    if (m_depthHandle.IsValid()) {
        GetDevice()->DestroyTexture(m_depthHandle);
    }
    m_depthHandle = GetDevice()->CreateTexture({
        .width = width, .height = height, .format = RHIFormat::D32_Float
    });
}
```

### OnShutdown() — Optional Cleanup Hook

```cpp
protected:
    virtual void OnShutdown() {}
```

Called before GPU shutdown. Release subclass-owned GPU resources here.

**When called**: Before device destruction, after `WaitIdle()` and `OnFrame()` stops
**Device state**: Still valid — can destroy resources
**Default**: Empty (no override needed if using RAII or handles)

**Example**:
```cpp
void MyDemo::OnShutdown() override {
    // Manually release resources if not using handles
    if (m_customBuffer) {
        GetDevice()->DestroyBuffer(m_customBuffer);
    }
}
```

---

## Accessors

### GetDevice() → IRHIDevice*

Returns the RHI device.

```cpp
IRHIDevice* device = GetDevice();
auto texHandle = device->CreateTexture({...});
```

### GetCommandList() → IRHICommandList*

Returns the primary graphics command list. Already opened (`Begin()` called) when `OnFrame()` is invoked.

```cpp
GetCommandList()->SetPipeline(m_pipeline);
GetCommandList()->Draw(6, 0);
```

### GetSwapChain() → IRHISwapChain*

Returns the swapchain for presentation.

```cpp
auto [width, height] = GetSwapChain()->GetDimensions();
```

### GetWindow() → PlatformWindow*

Returns the platform window object.

```cpp
bool shouldClose = GetWindow()->ShouldClose();
```

### GetFrameIndex() → uint32_t

Returns the current frame index (monotonically increasing from 0). Use this to index into per-frame data structures (constant buffers, descriptor tables, etc.) since `kFramesInFlight = 3`.

```cpp
uint32_t frameIdx = GetFrameIndex();
uint32_t cbufferSlot = frameIdx % 3;  // Which constant buffer to use this frame
GetCommandList()->SetBinding(0, m_cbuffers[cbufferSlot]);
```

### GetWidth() → uint32_t

Returns current window width in pixels.

```cpp
uint32_t w = GetWidth();
float aspect = (float)w / (float)GetHeight();
```

### GetHeight() → uint32_t

Returns current window height in pixels.

```cpp
uint32_t h = GetHeight();
```

### IsBindless() → bool

Returns true if the device supports SM6.6+ bindless resource access (descriptor tables without traditional root signature slots).

```cpp
if (IsBindless()) {
    // Use descriptor heap indices directly
} else {
    // Fall back to traditional root signature bindings
}
```

---

## Configuration via DemoConfig

Customize demo behavior by passing a `DemoConfig` struct to the constructor:

```cpp
/// Configuration for DemoApplication initialization.
///
/// All fields have sensible defaults for a windowed demo.
struct DemoConfig {
    const char* title = "UzEngine Demo";  ///< Window title bar text
    uint32_t width = 1280;                ///< Initial window width (pixels)
    uint32_t height = 720;                ///< Initial window height (pixels)
    uint32_t maxFrames = 0;               ///< Max frames to render (0 = unlimited)
    bool vsync = false;                   ///< Enable vertical sync on present
    bool enableValidation = true;         ///< Enable GPU validation layer (debug builds)
};
```

### Usage

You can pass `DemoConfig` directly:

```cpp
uz::DemoConfig config;
config.title = "My Advanced Demo";
config.width = 1920;
config.height = 1080;
config.maxFrames = 300;  // Run for 300 frames then exit
config.vsync = true;

class MyDemo : public uz::DemoApplication {
public:
    MyDemo(const uz::DemoConfig& config) : uz::DemoApplication(config) {}
    // ...
};

int main() {
    uz::LogInit();
    uz::DemoConfig config;
    config.title = "Advanced Demo";
    const int result = MyDemo(config).Run();
    uz::LogShutdown();
    return result;
}
```

Or use the convenience constructor with just a title:

```cpp
class MyDemo : public uz::DemoApplication {
public:
    MyDemo() : uz::DemoApplication("My Demo", 1280, 720) {}
};

int main() {
    uz::LogInit();
    const int result = MyDemo().Run();
    uz::LogShutdown();
    return result;
}
```

### Configuration Fields

| Field | Default | Purpose |
|-------|---------|---------|
| `title` | `"UzEngine Demo"` | Window title bar text displayed to user |
| `width` | `1280` | Initial window width in pixels |
| `height` | `720` | Initial window height in pixels |
| `maxFrames` | `0` | Maximum frames to render before auto-exit (0 = run indefinitely) |
| `vsync` | `false` | Enable vertical sync on present (matches monitor refresh rate) |
| `enableValidation` | `true` | Enable GPU validation/debug layer (release builds ignore this) |

---

## How to Create a Demo

### Step 1: Subclass DemoApplication

Create a header for your demo:

```cpp
// my_demo.h
#pragma once

#include "demo_application.h"
#include "rhi_types.h"

class MyDemo : public uz::DemoApplication {
public:
    MyDemo();
    ~MyDemo() override;

protected:
    void OnInit() override;
    void OnFrame(float dt) override;
    void OnResize(uint32_t width, uint32_t height) override;
    void OnShutdown() override;

private:
    // Your demo state
    uz::RHIPipelineHandle m_pipeline;
    uz::RHITextureHandle m_texture;
    uz::RHISamplerHandle m_sampler;
};
```

### Step 2: Implement the Hooks

```cpp
// my_demo.cpp
#include "my_demo.h"

MyDemo::MyDemo() : DemoApplication("My Demo", 1280, 720) {}

MyDemo::~MyDemo() = default;

void MyDemo::OnInit() {
    // Create shaders, textures, pipelines
    UZ_LOG(Demo, Info, "Initializing demo");

    // Compile shader
    auto vsHandle = CompileShader("shader.hlsl", ShaderStage::Vertex);

    // Create texture
    m_texture = GetDevice()->CreateTexture({
        .width = 256, .height = 256, .format = RHIFormat::RGBA8_Unorm
    });

    // Create pipeline
    m_pipeline = GetDevice()->CreatePipeline({...});
}

void MyDemo::OnFrame(float dt) {
    // Clear and draw
    GetCommandList()->ClearRenderTarget(
        GetSwapChain()->GetRenderTargetHandle(),
        {0.2f, 0.2f, 0.2f, 1.0f}
    );

    GetCommandList()->SetPipeline(m_pipeline);
    GetCommandList()->SetBinding(0, m_texture, m_sampler);
    GetCommandList()->Draw(6, 0);
}

void MyDemo::OnResize(uint32_t width, uint32_t height) {
    // Recreate size-dependent resources
    UZ_LOG(Demo, Info, "Resized to {}x{}", width, height);
}

void MyDemo::OnShutdown() {
    UZ_LOG(Demo, Info, "Shutting down demo");
    // Cleanup if needed
}
```

### Step 3: Write main()

```cpp
// main.cpp
#include "my_demo.h"

int main() {
    uz::LogInit();
    const int result = MyDemo().Run();
    uz::LogShutdown();
    return result;
}
```

### Step 4: Build

Link against the engine GPU libraries and run. The demo window opens automatically.

---

## Minimal Example: TexturedQuadDemo Pattern

The engine includes a complete example: `textured_quad_demo.h` and `main.cpp`. This demo:

1. **Generates a checkerboard texture** procedurally in CPU memory
2. **Uploads to GPU** via a staging buffer with transition barriers
3. **Creates a graphics pipeline** (automatically selects bindless or SM5 path)
4. **Renders a fullscreen quad** with the texture
5. **Validates** texture upload, SRV binding, sampler creation, and root constants

### Structure

```
textured_quad_demo.h
├── Public API
│   ├── Constructor (inherits DemoApplication)
│   └── Destructor
└── Protected hooks
    ├── OnInit() — Create texture, shader, pipeline, vertex buffer
    ├── OnFrame(dt) — Clear and draw quad
    └── OnShutdown() — Cleanup

main.cpp
└── int main()
    ├── uz::LogInit()
    ├── TexturedQuadDemo().Run()
    └── uz::LogShutdown()
```

### Key Implementation Details

**Checkerboard Texture**:
```cpp
void TexturedQuadDemo::CreateCheckerboardTexture() {
    std::vector<uint32_t> pixels(256 * 256);
    for (uint32_t y = 0; y < 256; y++) {
        for (uint32_t x = 0; x < 256; x++) {
            bool black = ((x / 16) + (y / 16)) % 2 == 0;
            pixels[y * 256 + x] = black ? 0xFF000000 : 0xFFFFFFFF;
        }
    }
    // Upload via staging buffer...
}
```

**Bindless vs SM5 Path**:
```cpp
void TexturedQuadDemo::CreatePipeline() {
    m_bindless = IsBindless();  // Check device capability

    uz::RHIPipelineDesc pipelineDesc{...};
    if (m_bindless) {
        // Use descriptor heap indices directly
        pipelineDesc.rootSigLayout.numTables = 1;
    } else {
        // Use traditional root signature slots
        pipelineDesc.rootSigLayout.numSlots = 2;
    }
    m_pipeline = GetDevice()->CreatePipeline(pipelineDesc);
}
```

---

## Build Integration

### CMake (Primary)

```cmake
# CMakeLists.txt for your demo
add_executable(my_demo
    my_demo.h
    my_demo.cpp
    main.cpp
)

target_link_libraries(my_demo PRIVATE
    uz_engine_gpu  # RHI abstractions
    uz_core        # Logging, assertions
)
```

### Zig Build System (Alternative)

The engine also supports Zig's build system for demos. See `build.zig`:

```zig
const demo_exe = b.addExecutable(.{
    .name = "my_demo",
    .root_source_file = b.path("src/main.cpp"),
    .target = target,
    .optimize = optimize,
});

demo_exe.linkLibrary(uz_core);
demo_exe.linkLibrary(uz_gpu);
demo_exe.linkSystemLibrary("d3d12");  // For DX12 backend
```

---

## Frame Timing

Each frame, `OnFrame(dt)` receives delta time in seconds:

```cpp
void MyDemo::OnFrame(float dt) {
    float angleChange = 45.0f * dt;  // Degrees per second
    m_cameraAngle += angleChange;
}
```

Delta time is clamped to `0.1s` max to prevent explosions after:
- Debugger breakpoints
- System stalls
- Window dragging or message pumps

---

# Migration to EngineLoop

> **Target Audience**: Demo authors wanting to simplify their initialization code
> **Scope**: How to migrate an existing demo from manual main() to `EngineLoop::Run()`
> **Time**: ~15 minutes per demo

## Best Practices

### 1. Always Initialize Logging

```cpp
int main() {
    uz::LogInit();
    const int result = MyDemo().Run();
    uz::LogShutdown();
    return result;
}
```

`DemoApplication` logs initialization progress, shutdown stats, and errors. Set up logging before `Run()` so you see all messages.

### 2. Use Handle Types for GPU Resources

Don't store raw pointers to GPU objects. Use `RHIPipelineHandle`, `RHITextureHandle`, `RHIBufferHandle`, etc. These are move-only and lifetime-safe.

```cpp
// ✅ CORRECT
class MyDemo : public DemoApplication {
private:
    uz::RHIPipelineHandle m_pipeline;  // Moved, not copied
    uz::RHITextureHandle m_texture;
};

// ❌ WRONG
class MyDemo : public DemoApplication {
private:
    ID3D12PipelineState* m_pipeline;  // Raw pointer, lifetime issues
};
```

### 3. Check Device Capabilities

Use `IsBindless()` to select code paths for different GPU generations:

```cpp
void MyDemo::OnInit() override {
    if (IsBindless()) {
        // SM6.6+: Use descriptor heap indices directly
        CreatePipelineWithDescriptorHeaps();
    } else {
        // SM5: Fall back to root signature slots
        CreatePipelineWithRootSignature();
    }
}
```

### 4. Index Into Per-Frame Data

With `kFramesInFlight = 3`, use `GetFrameIndex() % 3` to safely index per-frame data (constant buffers, descriptor tables):

```cpp
void MyDemo::OnFrame(float dt) {
    uint32_t frameIdx = GetFrameIndex() % 3;

    // Update frame-specific constant buffer
    GetCommandList()->SetBinding(0, m_cbuffers[frameIdx]);

    // Record commands...
}
```

### 5. Handle Resize in OnResize()

Don't assume window dimensions are constant. Override `OnResize()` if you have size-dependent resources:

```cpp
void MyDemo::OnResize(uint32_t width, uint32_t height) override {
    // Depth buffer must match window size
    if (m_depthBuffer.IsValid()) {
        GetDevice()->DestroyTexture(m_depthBuffer);
    }
    m_depthBuffer = GetDevice()->CreateTexture({
        .width = width,
        .height = height,
        .format = RHIFormat::D32_Float
    });

    // Update projection matrix
    float aspect = (float)width / (float)height;
    m_projection = uz::Matrix4x4::Perspective(45.0f, aspect, 0.1f, 1000.0f);
}
```

### 6. Don't Close the Command List

The base class manages command list lifecycle. Record commands but don't call:
- `GetCommandList()->End()`
- `GetCommandList()->Execute()`

The base class does this after your `OnFrame()` returns.

```cpp
void MyDemo::OnFrame(float dt) {
    // ✅ CORRECT
    GetCommandList()->SetPipeline(m_pipeline);
    GetCommandList()->Draw(6, 0);
    // Return — base class closes and executes

    // ❌ WRONG
    // GetCommandList()->End();  // Don't do this!
    // GetCommandList()->Execute();  // Don't do this!
}
```

### 7. Clamp Delta Time Manually if Needed

`OnFrame()` receives `dt` clamped to 0.1s. If you need more control:

```cpp
void MyDemo::OnFrame(float dt) {
    static float accumulated = 0.0f;
    accumulated += dt;

    // Simulate at fixed timestep
    while (accumulated >= kFixedTimestep) {
        UpdatePhysics(kFixedTimestep);
        accumulated -= kFixedTimestep;
    }

    Render();
}
```

### 8. Test Different Configurations

Test with different `DemoConfig` settings:

```cpp
// Fast iteration (no vsync, validate every frame)
uz::DemoConfig fast{
    .title = "My Demo",
    .vsync = false,
    .enableValidation = true,
};

// Production (vsync, no validation overhead)
uz::DemoConfig prod{
    .title = "My Demo",
    .vsync = true,
    .enableValidation = false,
};

// Headless automated test (fixed frame count)
uz::DemoConfig test{
    .title = "My Demo Test",
    .maxFrames = 300,
    .enableValidation = true,
};
```

---

## TexturedQuadDemo — Complete Example

The engine includes a fully-realized example: `demos/textured_quad/`. It demonstrates:

1. Procedural texture generation (checkerboard)
2. GPU upload pipeline (staging buffer + state transitions)
3. Shader compilation and pipeline creation
4. Bindless vs SM5 path selection
5. Simple draw call with samplers

### Header

**File**: `runtime/demos/textured_quad/textured_quad_demo.h`

```cpp
/// Textured quad demo — draws a checkerboard quad using the RHI abstraction.
///
/// Extends `DemoApplication` to eliminate window/device/swapchain boilerplate.
/// All GPU resources are created in `OnInit()` and released in `OnShutdown()`.
class TexturedQuadDemo final : public DemoApplication {
public:
    TexturedQuadDemo();
    ~TexturedQuadDemo() override;

protected:
    void OnInit() override;
    void OnFrame(float dt) override;
    void OnShutdown() override;

private:
    /// Generates the 256x256 procedural checkerboard, creates GPU texture,
    /// uploads via staging buffer, and transitions to ShaderResource state.
    void CreateCheckerboardTexture();

    /// Compiles vertex/pixel shaders via ShaderPipeline.
    void CompileShaders();

    /// Creates the graphics pipeline (bindless or SM5 input-assembled).
    void CreatePipeline();

    /// Creates the quad vertex buffer (6 verts, 2 triangles).
    void CreateVertexBuffer();

    // ── GPU resources ───────────────────────────────────────────────────
    RHIPipelineHandle pipeline_;
    RHIShaderHandle vsHandle_;
    RHIShaderHandle psHandle_;
    RHIBufferHandle vbHandle_;
    RHITextureHandle texHandle_;
    RHISamplerHandle samplerHandle_;

    uint32_t texSrvIndex_ = UINT32_MAX;
    uint32_t vbSrvIndex_ = 0;
    uint32_t samplerIdx_ = UINT32_MAX;

    bool bindless_ = false;

    std::unique_ptr<ShaderPipeline> shaderPipeline_;
    std::unique_ptr<ShaderCompiler> compiler_;
    std::unique_ptr<ShaderCompilerFXC> compilerFXC_;
};
```

### Implementation Overview

**OnInit()**:
1. Create shader compiler
2. Compile vertex and pixel shaders
3. Create checkerboard texture
4. Create graphics pipeline
5. Create vertex buffer

```cpp
void TexturedQuadDemo::OnInit() {
    // Determine bindless capability
    bindless_ = IsBindless();

    // Initialize shader compilation
    shaderPipeline_ = std::make_unique<ShaderPipeline>();
    compiler_ = shaderPipeline_->CreateCompiler(GetDevice());
    compilerFXC_ = std::make_unique<ShaderCompilerFXC>();

    // Compile shaders from "shaders/quad.hlsl"
    CompileShaders();

    // Create checkerboard texture (procedural + GPU upload)
    CreateCheckerboardTexture();

    // Create pipeline (bindless or SM5)
    CreatePipeline();

    // Create vertex buffer (6 verts: 2 triangles)
    CreateVertexBuffer();

    UZ_LOG(Demo, Info, "TexturedQuadDemo initialized");
}
```

**OnFrame()**:
1. Clear render target to light blue
2. Set pipeline and bindings
3. Draw 6 vertices (fullscreen quad)

```cpp
void TexturedQuadDemo::OnFrame(float dt) {
    // Clear to light blue
    GetCommandList()->ClearRenderTarget(
        GetSwapChain()->GetRenderTargetHandle(),
        {0.2f, 0.4f, 0.8f, 1.0f}
    );

    // Set pipeline and resources
    GetCommandList()->SetPipeline(pipeline_);

    if (bindless_) {
        // Bindless path: use descriptor heap indices
        GetCommandList()->SetBinding(0, texSrvIndex_);
        GetCommandList()->SetBinding(1, samplerIdx_);
    } else {
        // SM5 path: traditional slots
        GetCommandList()->SetBinding(0, texHandle_);
        GetCommandList()->SetBinding(1, samplerHandle_);
    }

    // Draw quad
    GetCommandList()->SetVertexBuffer(0, vbHandle_);
    GetCommandList()->Draw(6, 0);
}
```

**OnShutdown()**:
- Default empty (handles are cleaned up automatically)

```cpp
void TexturedQuadDemo::OnShutdown() {
    // Handles are destroyed automatically
}
```

### Key Validation Points

This demo validates:

| Component | How | Expected Result |
|-----------|-----|---|
| **Texture Upload** | Procedural CPU→GPU via staging buffer | Checkerboard appears on screen |
| **State Transitions** | Staging → ShaderResource transition | GPU can sample texture |
| **SRV Binding** | Bindless or SM5 root signature | Correct texture lookup |
| **Sampler Creation** | Point sampler for checkerboard | Sharp pixelated pattern |
| **Root Constants** | Per-frame root constants (if used) | Correct frame data |
| **Quad Vertex Buffer** | 6 verts covering viewport | Fullscreen quad |

---

## Entry Point

**File**: `runtime/demos/textured_quad/main.cpp`

```cpp
#include "textured_quad_demo.h"
#include "log.h"

int main() {
    uz::LogInit();
    uz::TexturedQuadDemo demo;
    const int result = demo.Run();
    uz::LogShutdown();
    return result;
}
```

---

## Build Integration

### CMake (Simple)

```cmake
add_executable(textured_quad_demo
    textured_quad_demo.h
    textured_quad_demo.cpp
    main.cpp
)

target_link_libraries(textured_quad_demo PRIVATE
    uz_engine_gpu
    uz_core
    uz_shader_compiler
)
```

### Zig Build System

The engine's `build.zig` includes demo targets. Each demo is an executable that depends on engine libraries:

```zig
const textured_quad = b.addExecutable(.{
    .name = "textured_quad",
    .root_source_file = b.path("runtime/demos/textured_quad/main.cpp"),
    .target = target,
    .optimize = optimize,
});

textured_quad.linkLibrary(uz_core);
textured_quad.linkLibrary(uz_gpu);
textured_quad.linkLibrary(uz_shader_compiler);

// Windows-specific: link D3D12
if (target.result.os.tag == .windows) {
    textured_quad.linkSystemLibrary("d3d12");
    textured_quad.linkSystemLibrary("dxgi");
    textured_quad.linkSystemLibrary("d3dcompiler");
}

b.installArtifact(textured_quad);
```

### Running the Demo

```bash
# CMake
cmake --build build --target textured_quad_demo --config Release
./build/Release/textured_quad_demo

# Zig
zig build -p install textured_quad
./zig-cache/bin/textured_quad
```

Expected output:
- Window opens with fullscreen blue background
- Checkerboard pattern rendered in center
- FPS counter in console
- Clean shutdown on close

---

## Common Patterns

### Pattern: Multi-Pass Rendering

```cpp
class MultiPassDemo : public DemoApplication {
    void OnFrame(float dt) override {
        // Pass 1: Render to offscreen target
        GetCommandList()->SetRenderTarget(m_rtHandle);
        GetCommandList()->ClearRenderTarget(m_rtHandle, {0, 0, 0, 1});
        GetCommandList()->SetPipeline(m_passPipeline);
        GetCommandList()->Draw(6, 0);

        // Transition: RT → ShaderResource
        GetCommandList()->Barrier({
            .resource = m_rtHandle,
            .stateBefore = RHIResourceState::RenderTarget,
            .stateAfter = RHIResourceState::ShaderResource,
        });

        // Pass 2: Render to backbuffer using Pass 1 output
        GetCommandList()->SetRenderTarget(GetSwapChain()->GetRenderTargetHandle());
        GetCommandList()->SetPipeline(m_compositePipeline);
        GetCommandList()->SetBinding(0, m_rtHandle);
        GetCommandList()->Draw(6, 0);
    }

private:
    RHITextureHandle m_rtHandle;
    RHIPipelineHandle m_passPipeline;
    RHIPipelineHandle m_compositePipeline;
};
```

### Pattern: Animated Parameter

```cpp
class AnimatedDemo : public DemoApplication {
    void OnFrame(float dt) override {
        m_angle += 45.0f * dt;  // 45 degrees/sec

        // Update constant buffer
        uint32_t frameIdx = GetFrameIndex() % 3;
        struct CB {
            float angle;
            float pad[3];
        } cb{m_angle};

        GetCommandList()->UpdateBuffer(m_cbBuffer[frameIdx], 0, &cb, sizeof(cb));

        // Render with updated constant
        GetCommandList()->SetBinding(0, m_cbBuffer[frameIdx]);
        GetCommandList()->Draw(6, 0);
    }

private:
    float m_angle = 0.0f;
    RHIBufferHandle m_cbBuffer[3];
};
```

### Pattern: Resize-Dependent Depth Buffer

```cpp
class DepthBufferDemo : public DemoApplication {
    void OnInit() override {
        CreateDepthBuffer(GetWidth(), GetHeight());
    }

    void OnResize(uint32_t width, uint32_t height) override {
        if (m_depthBuffer.IsValid()) {
            GetDevice()->DestroyTexture(m_depthBuffer);
        }
        CreateDepthBuffer(width, height);
    }

    void OnFrame(float dt) override {
        // Render with depth
        GetCommandList()->ClearDepth(m_depthBuffer, 1.0f);
        GetCommandList()->SetDepthBuffer(m_depthBuffer);
        GetCommandList()->Draw(m_vertexCount, 0);
    }

private:
    RHITextureHandle m_depthBuffer;

    void CreateDepthBuffer(uint32_t w, uint32_t h) {
        m_depthBuffer = GetDevice()->CreateTexture({
            .width = w,
            .height = h,
            .format = RHIFormat::D32_Float,
        });
    }
};
```

---

## Before and After

### Before (Manual Boilerplate)

Every demo currently needs ~200+ lines of manual setup:

```cpp
// old_demo.cpp
int main(int argc, char* argv[]) {
    // 1. Initialize logging
    uz::LogSystem::Initialize();
    UZ_LOG(Demo, Info, "Starting demo");

    // 2. Load config
    uz::ConfigSystem::Load("project.uzproject");

    // 3. Create window
    uz::PlatformWindow* window = uz::PlatformWindow::Create({
        .width = 1920,
        .height = 1080,
        .title = "My Demo",
    });
    if (!window) {
        UZ_LOG(Demo, Error, "Failed to create window");
        return 1;
    }

    // 4. Initialize RHI
    uz::IRHIDevice* device = uz::CreateRHIDevice({
        .backend = uz::RHIBackend::DX12,
    });
    if (!device) {
        UZ_LOG(Demo, Error, "Failed to create RHI device");
        window->Destroy();
        return 1;
    }

    // 5. Create swap chain
    uz::IRHISwapChain* swap_chain = device->CreateSwapChain({
        .window = window,
        .format = uz::RHIFormat::RGBA8_Unorm_Srgb,
    });
    if (!swap_chain) {
        UZ_LOG(Demo, Error, "Failed to create swap chain");
        device->Destroy();
        window->Destroy();
        return 1;
    }

    // 6. Initialize renderer
    if (!uz::RendererSystem::Initialize(device, swap_chain)) {
        UZ_LOG(Demo, Error, "Failed to initialize renderer");
        swap_chain->Destroy();
        device->Destroy();
        window->Destroy();
        return 1;
    }

    // 7. Main loop
    bool quit = false;
    while (!quit) {
        // Poll input
        uz::InputSystem::Poll();
        if (window->ShouldClose()) quit = true;

        // Update
        float dt = GetDeltaTime();  // Manual timing calculation
        UpdateDemo(dt);

        // Render
        uz::CommandList* cmd = device->BeginFrame(swap_chain);
        RenderDemo(cmd);
        device->EndFrame(swap_chain);
        swap_chain->Present();
    }

    // 8. Shutdown (must be reverse order!)
    uz::RendererSystem::Shutdown();
    swap_chain->Destroy();
    device->Destroy();
    window->Destroy();
    uz::LogSystem::Shutdown();

    return 0;
}
```

### After (EngineLoop)

Using `EngineLoop::Run()`, the same demo becomes:

```cpp
// new_demo.cpp
class DemoSubsystem final : public uz::ISubsystem {
public:
    const char* GetName() const override { return "Demo"; }
    uz::InitPhase GetInitPhase() const override { return uz::InitPhase::Start; }
    uz::TickGroup GetTickGroup() const override { return uz::TickGroup::Update; }

    std::vector<const char*> GetDependencies() const override {
        return {"Renderer"};
    }

    bool Init() override {
        UZ_LOG(Demo, Info, "Demo initialized");
        return true;
    }

    void Tick(float deltaTime) override {
        UpdateDemo(deltaTime);
    }

    void Shutdown() override {
        UZ_LOG(Demo, Info, "Demo shutdown");
    }
};

UZ_REGISTER_MODULE(DemoSubsystem);

int main(int argc, char* argv[]) {
    uz::EngineLoop loop;
    return loop.Run(argc, argv);
}
```

**Reduction**: From 100+ lines to ~5 lines in `main()`. The EngineLoop handles:
- Logging, config, paths initialization
- Window creation
- RHI device and swap chain creation
- Renderer setup
- Main loop with timing
- Ordered shutdown in reverse

## Step-by-Step Migration

### Step 1: Extract Demo Logic into a Subsystem

Create a new subsystem for your demo-specific code:

```cpp
// demo.h
class MyDemoSubsystem final : public uz::ISubsystem {
public:
    const char* GetName() const override { return "MyDemo"; }
    uz::InitPhase GetInitPhase() const override { return uz::InitPhase::Start; }
    uz::TickGroup GetTickGroup() const override { return uz::TickGroup::Update; }

    bool Init() override;
    void Shutdown() override;
    void Tick(float deltaTime) override;

private:
    // Your demo state here
};

// demo.cpp
bool MyDemoSubsystem::Init() {
    // What you had in step 6-7 (after renderer init)
    UZ_LOG(Demo, Info, "Initializing demo");
    return true;
}

void MyDemoSubsystem::Tick(float deltaTime) {
    // Your main loop body here
    UpdateDemo(deltaTime);
    RenderDemo(deltaTime);
}

void MyDemoSubsystem::Shutdown() {
    // Cleanup
    UZ_LOG(Demo, Info, "Shutting down demo");
}

UZ_REGISTER_MODULE(MyDemoSubsystem);
```

### Step 2: Simplify main()

Replace all the initialization boilerplate with `EngineLoop::Run()`:

```cpp
// main.cpp
int main(int argc, char* argv[]) {
    uz::EngineLoop loop;
    return loop.Run(argc, argv);
}
```

That's it. The `EngineLoop` will:
1. Parse CLI args
2. Register all modules (including your demo via `UZ_REGISTER_MODULE`)
3. Initialize all subsystems in dependency order
4. Run the main loop
5. Shut down in reverse order

### Step 3: Move Rendering to the Renderer

If you have custom rendering code, move it to the Renderer subsystem:

```cpp
// Before: Demo subsystem does rendering
void MyDemoSubsystem::Tick(float deltaTime) {
    UpdateDemo(deltaTime);
    RenderDemo(deltaTime);  // ← Demo handles rendering
}

// After: Renderer subsystem handles rendering
void MyDemoSubsystem::Tick(float deltaTime) {
    UpdateDemo(deltaTime);
    // Renderer handles RenderDemo() in its own tick
}
```

The `Renderer` subsystem ticks in `PostUpdate` group, after game logic, so you can queue render commands during your `Update` tick.

### Step 4: Declare Dependencies

If your demo needs subsystems, declare them:

```cpp
std::vector<const char*> GetDependencies() const override {
    return {"Renderer", "Physics", "Audio"};
}
```

The registry will ensure these initialize before your demo.

### Step 5: Test with CLI Flags

The new `EngineLoop` supports CLI flags for testing:

```bash
# Run demo for 300 frames then exit (for CI/automated testing)
demo.exe --frames 300

# Run demo in headless mode (no window, useful for servers)
demo.exe --headless

# Capture a screenshot at frame 150
demo.exe --screenshot-frame 150 --screenshot "output.png"

# Use a custom config file
demo.exe --config "custom_project.uzproject"
```

## Common Migration Patterns

### Pattern 1: Simple Render Loop

**Before**:
```cpp
int main(int argc, char* argv[]) {
    // ... 100 lines of init ...
    while (!quit) {
        // Manual timing
        static double last_time = 0;
        double now = GetCurrentTime();
        float dt = now - last_time;
        last_time = now;

        // Update and render
        camera.Update(dt);
        renderer.Render(scene);
        swap_chain->Present();
    }
    // ... 20 lines of shutdown ...
}
```

**After**:
```cpp
class DemoSubsystem final : public uz::ISubsystem {
    const char* GetName() const override { return "Demo"; }
    uz::InitPhase GetInitPhase() const override { return uz::InitPhase::Start; }
    uz::TickGroup GetTickGroup() const override { return uz::TickGroup::Update; }

    bool Init() override { return true; }
    void Tick(float dt) override {
        camera.Update(dt);
        renderer.Render(scene);
    }
    void Shutdown() override { }
};
UZ_REGISTER_MODULE(DemoSubsystem);

int main(int argc, char* argv[]) {
    uz::EngineLoop loop;
    return loop.Run(argc, argv);
}
```

### Pattern 2: Multi-Subsystem Demo

If your demo uses multiple systems (physics, audio, rendering):

**Before**:
```cpp
int main() {
    // Init logging, config, paths, window, RHI
    InitLogging();
    InitConfig();
    InitPaths();
    InitWindow();
    InitRHI();

    // Init systems
    PhysicsSystem physics;
    physics.Initialize();

    AudioSystem audio;
    audio.Initialize();

    RendererSystem renderer;
    renderer.Initialize();

    // Main loop with manual ordering
    while (!quit) {
        float dt = ComputeDeltaTime();
        physics.Update(dt);
        audio.Update(dt);
        renderer.Render();
    }

    // Shutdown in reverse
    renderer.Shutdown();
    audio.Shutdown();
    physics.Shutdown();
    ShutdownRHI();
    ShutdownWindow();
}
```

**After**:
```cpp
// Each system is a subsystem with declared dependencies
class PhysicsSubsystem : public uz::ISubsystem {
    const char* GetName() const override { return "Physics"; }
    uz::InitPhase GetInitPhase() const override { return uz::InitPhase::Init; }
    uz::TickGroup GetTickGroup() const override { return uz::TickGroup::PostPhysics; }
    bool Init() override { /* ... */ return true; }
    void Tick(float dt) override { /* Update physics */ }
    void Shutdown() override { }
};
UZ_REGISTER_MODULE(PhysicsSubsystem);

// Ordering and shutdown are automatic!
int main(int argc, char* argv[]) {
    uz::EngineLoop loop;
    return loop.Run(argc, argv);
}
```

### Pattern 3: Interactive Demo with Input

**Before**:
```cpp
int main() {
    // ... init ...
    while (!quit) {
        // Manual input handling
        if (input.IsKeyPressed(Key::Escape)) quit = true;
        if (input.IsKeyPressed(Key::Space)) DoSomething();

        Update(dt);
        Render();
    }
    // ... shutdown ...
}
```

**After**:
```cpp
class DemoSubsystem : public uz::ISubsystem {
    // ... boilerplate ...
    void Tick(float dt) override {
        // InputSystem has already polled input
        // Query it here
        if (uz::InputSystem::IsKeyPressed(uz::Key::Escape)) {
            RequestShutdown();  // Signal engine loop to exit
        }
        if (uz::InputSystem::IsKeyPressed(uz::Key::Space)) {
            DoSomething();
        }
        Update(dt);
    }
};
UZ_REGISTER_MODULE(DemoSubsystem);

int main(int argc, char* argv[]) {
    uz::EngineLoop loop;
    return loop.Run(argc, argv);
}
```

### Pattern 4: Headless/Server Demo

For automated testing or server builds:

**Before**:
```cpp
int main(int argc, char* argv[]) {
    // ... init with window creation ...
    // Hard to run without a window
}
```

**After**:
```cpp
class DemoSubsystem : public uz::ISubsystem {
    // ... standard subsystem ...
};
UZ_REGISTER_MODULE(DemoSubsystem);

int main(int argc, char* argv[]) {
    uz::EngineLoop loop;
    // Automatically handles --headless flag
    // No window created, no GPU calls made
    return loop.Run(argc, argv);
}
```

Run it:
```bash
demo.exe --headless --frames 100  # Run 100 frames without window
```

## Checklist: Migrating Your Demo

Use this checklist to migrate your demo:

- [ ] Create a new subsystem class inheriting from `ISubsystem`
- [ ] Implement `GetName()` (return a unique name for your demo)
- [ ] Implement `GetInitPhase()` (usually `InitPhase::Start` for demos)
- [ ] Implement `GetTickGroup()` (usually `TickGroup::Update` for game logic)
- [ ] Declare `GetDependencies()` for any systems you need (Renderer, Physics, etc.)
- [ ] Move demo initialization from `main()` to `Init()` method
- [ ] Move demo main loop from `main()` to `Tick()` method
- [ ] Move demo cleanup from `main()` to `Shutdown()` method
- [ ] Register subsystem with `UZ_REGISTER_MODULE(DemoSubsystem)`
- [ ] Replace `main()` with simple `EngineLoop::Run()` call
- [ ] Test with CLI flags: `--frames 100`, `--headless`, `--screenshot`
- [ ] Verify that subsystems initialize in correct order (check logs)
- [ ] Verify that demo shuts down cleanly (no GPU leaks)

## Troubleshooting Common Migration Issues

### "My subsystem's Init() fails silently"

**Cause**: Your `Init()` returns `false`, but you didn't check the logs.

**Fix**:
1. Check the logs for error messages from your `Init()` method
2. Make sure you're calling `UZ_LOG()` to report what went wrong
3. The engine loop will roll back and shut down cleanly

### "Subsystem ticks but dependencies aren't initialized yet"

**Cause**: You forgot to declare dependencies in `GetDependencies()`.

**Fix**: List all subsystems you access in `Tick()`:

```cpp
std::vector<const char*> GetDependencies() const override {
    return {"Renderer", "Physics", "Audio"};  // Add what you need
}
```

### "Rendering doesn't work in my subsystem"

**Cause**: You're ticking in `PrePhysics` or `PostPhysics`, but the Renderer hasn't executed yet.

**Fix**: Tick in `Update` or `PostUpdate`, which are called after physics and before rendering:

```cpp
uz::TickGroup GetTickGroup() const override {
    return uz::TickGroup::PostUpdate;  // After physics, before render
}
```

### "My demo runs for one frame then exits"

**Cause**: The `--frames` CLI flag is set to 1.

**Fix**: Either:
- Don't pass `--frames` (unlimited by default)
- Or pass `--frames 0` for unlimited
- Or remove the flag from your test script

### "Missing subsystem: 'Renderer'"

**Cause**: You declared a dependency on "Renderer" but the Renderer subsystem wasn't registered.

**Fix**:
1. Check that the Renderer subsystem actually exists and is registered
2. Verify the name matches exactly (case-sensitive): `"Renderer"` not `"renderer"`
3. Check that `UZ_REGISTER_MODULE()` is being called

## Performance Tips

### Benchmark Your Demo

Use the timing output from logs:

```
Lifecycle: Initializing subsystem: TimeManager
Lifecycle: TimeManager initialized in 0.002ms

Lifecycle: Initializing subsystem: Renderer
Lifecycle: Renderer initialized in 125.432ms
```

Slow subsystems (over 100ms) should be optimized.

### Use `--frames` for Benchmarking

Run a fixed number of frames to get stable timing:

```bash
# Run 1000 frames and measure
demo.exe --frames 1000
```

This gives you:
- No UI/OS latency from window events
- Deterministic timing (same inputs every frame)
- Can be automated in CI/CD pipelines

### Profiling with Tracy

If you have Tracy integration, your subsystems will automatically participate:

```cpp
void MySubsystem::Tick(float dt) {
    UZ_PROFILE_SCOPE("Demo::Tick");  // Named zone in Tracy
    UpdateDemo(dt);
}
```

## Moving Existing Demos

### Example: Converting a Clear-Screen Demo

**Original** (`demos/clear-screen/main.cpp` — ~150 lines):
```cpp
int main(int argc, char* argv[]) {
    // ... 40 lines of init ...
    while (!quit) {
        // ... 20 lines of loop ...
    }
    // ... 30 lines of shutdown ...
}
```

**Refactored** (split into two files):

**demos/clear-screen/subsystem.h** (new):
```cpp
class ClearScreenDemo : public uz::ISubsystem {
    // ... interface methods ...
};
```

**demos/clear-screen/main.cpp** (simplified):
```cpp
int main(int argc, char* argv[]) {
    uz::EngineLoop loop;
    return loop.Run(argc, argv);
}
```

## Next Steps

After migrating your demo:

1. **Verify logs**: Check that subsystems initialize in the correct order
2. **Test CLI flags**: Try `--frames 100`, `--headless`, `--screenshot`
3. **Profile startup**: The engine loop logs each subsystem's init time
4. **Add more subsystems**: If your demo grows, split it into multiple subsystems
5. **Share patterns**: If you find a useful pattern, document it for other demos

## See Also

- [Application Lifecycle — README](./README.md) — Full system overview
- [Subsystem Registry Documentation](./04-subsystem-registry.md) — Detailed API
- [Engine Loop Documentation](./02-bootstrap.md) — Initialization phases
