# Demo Migration Guide — From Manual Boilerplate to EngineLoop

> **Target Audience**: Demo authors wanting to simplify their initialization code
> **Scope**: How to migrate an existing demo from manual main() to `EngineLoop::Run()`
> **Time**: ~15 minutes per demo

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
