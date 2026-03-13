# Testing & Automation Framework

> **Status**: Planned (Phase 4)
> **Source**: `developer/automation/`
> **References**: UE5 Automation Framework, CTest, Google Test (clean-room)

## Purpose

Automated testing framework for CI/CD pipelines and regression testing:
- **Headless mode** — run game without rendering for fast test execution
- **Test framework** — register test functions, automatic discovery, reporting
- **Screenshot capture** — save backbuffer for visual regression testing
- **Replay system** — record inputs, replay exact same sequence (deterministic)
- **Performance benchmarks** — measure frame time, memory, draw calls

## Headless Mode

Run engine without window/rendering:

```bash
$ game.exe --headless --run-tests

# Or in code:
uz::EngineConfig config;
config.headless = true;
uz::Engine engine(config);
```

Benefits:
- No rendering overhead (10-100x faster)
- CI-friendly (no X11 or Wayland required)
- Deterministic frame timing (no vsync)
- Easy test automation

### Implementation

```cpp
namespace uz::dev {

struct EngineConfig {
    bool headless = false;  ///< Skip window creation, rendering
};

class Engine {
public:
    void Run() {
        if (config_.headless) {
            RunHeadless();
        } else {
            RunNormal();
        }
    }

    void RunHeadless() {
        // Skip window creation, RHI device initialization
        // Just run game loop at max speed
        while (running_) {
            Update();
            // No rendering, no vsync
        }
    }
};

}  // namespace uz::dev
```

## Test Framework

Register and run tests:

```cpp
namespace uz::dev {

/// Test function signature
using TestFn = std::function<bool(World& world)>;

/// Register a test (called during static initialization)
void RegisterTest(StringView name, TestFn fn);

/// Run all tests, return results
struct TestResults {
    int passed = 0, failed = 0, skipped = 0;
    std::vector<std::string> failures;
    std::vector<float> frame_times_ms;
};

TestResults RunAllTests();

}  // namespace uz::dev
```

### Test Examples

```cpp
// Test physics
uz::dev::RegisterTest("physics_raycast", [](uz::World& world) {
    auto physics = world.GetPhysicsWorld();

    // Create a ground plane
    auto ground = world.Create();
    world.SetPosition(ground, Vec3(0, 0, 0));
    auto rb = world.Attach<Rigidbody>(ground);
    rb->SetBodyType(RigidbodyType::Static);

    // Raycast down from above
    Vec3 from(0, 100, 0);
    Vec3 dir = Vec3::Down();
    RaycastHit hit;

    if (!physics->Raycast(from, dir, 1000, hit)) {
        UZ_LOG(Test, Error, "Raycast failed: expected hit");
        return false;
    }

    // Check hit distance
    if (hit.distance < 99 || hit.distance > 101) {
        UZ_LOG(Test, Error, "Raycast distance wrong: {}", hit.distance);
        return false;
    }

    return true;  // Test passed
});

// Test animation
uz::dev::RegisterTest("animation_playback", [](uz::World& world) {
    auto entity = world.Create();
    auto anim = world.Attach<SkeletalMeshComponent>(entity);
    anim->PlayAnimation("idle", false);

    // Advance 10 frames
    for (int i = 0; i < 10; ++i) {
        world.Tick(1.0f / 60.0f);
    }

    float anim_time = anim->GetCurrentAnimationTime();
    if (anim_time < 0.15f || anim_time > 0.18f) {
        UZ_LOG(Test, Error, "Animation time wrong: {}", anim_time);
        return false;
    }

    return true;
});

// Test behavior tree
uz::dev::RegisterTest("behavior_tree_sequence", [](uz::World& world) {
    auto entity = world.Create();
    auto bt = world.Attach<BehaviorTreeComponent>(entity);
    bt->StartBehavior("test/sequence.uzbt");

    // Advance tree execution
    for (int i = 0; i < 100; ++i) {
        world.Tick(1.0f / 60.0f);
    }

    // Check final state
    if (bt->GetState() != BehaviorState::Success) {
        return false;
    }

    return true;
});
```

## Screenshot Capture and Regression Testing

```cpp
namespace uz::dev {

/// Capture backbuffer to PNG
void CaptureScreenshot(StringView filename);

/// Compare two PNG files per-pixel
/// Returns difference ratio (0.0 = identical, 1.0 = completely different)
float CompareScreenshots(StringView golden_path, StringView actual_path);

/// Screenshot regression test with threshold
bool ScreenshotTest(StringView golden_path, StringView actual_path, float tolerance = 0.01f);

}  // namespace uz::dev
```

### Usage

```cpp
uz::dev::RegisterTest("render_pbr_sphere", [](uz::World& world) {
    // Spawn sphere with PBR material
    auto sphere = world.Create();
    world.SetPosition(sphere, Vec3(0, 0, 0));
    auto mesh = world.Attach<StaticMeshComponent>(sphere);
    mesh->SetMesh("content/meshes/sphere.uzmesh");
    mesh->SetMaterial("content/materials/pbr.uzmat");

    // Advance frame (render)
    world.Tick(1.0f / 60.0f);

    // Capture result
    uz::dev::CaptureScreenshot("test_output/pbr_sphere_actual.png");

    // Compare to golden image
    float diff = uz::dev::CompareScreenshots(
        "test_golden/pbr_sphere.png",
        "test_output/pbr_sphere_actual.png"
    );

    // Allow 1% pixel difference (lighting, rounding)
    return diff < 0.01f;
});
```

## Replay System

Record input and replay for deterministic test execution:

```cpp
namespace uz::dev {

class InputRecorder {
public:
    /// Start recording input
    void StartRecording(StringView output_file);

    /// Stop recording
    void StopRecording();

    /// Record a single input
    void RecordInput(const InputEvent& input);

    /// Load recorded input
    bool LoadRecording(StringView input_file);

    /// Get next recorded input (if replaying)
    bool GetNextInput(InputEvent& out_input);

private:
    std::vector<std::pair<float, InputEvent>> recorded_inputs_;  // timestamp, input
};

}  // namespace uz::dev
```

### Usage

```cpp
// Record a gameplay session
{
    InputRecorder recorder;
    recorder.StartRecording("test_data/player_movement.inputs");

    uz::Engine engine;
    engine.Run();  // User plays

    recorder.StopRecording();
}

// Replay for testing
uz::dev::RegisterTest("replay_player_movement", [](uz::World& world) {
    InputRecorder replay;
    replay.LoadRecording("test_data/player_movement.inputs");

    // Feed recorded inputs to engine
    while (auto input = replay.GetNextInput()) {
        input_system->ProcessInput(*input);
        world.Tick(1.0f / 60.0f);
    }

    // Verify final state
    auto player = world.FindEntity("Player");
    return player != nullptr;
});
```

## Performance Benchmarks

Measure frame time, memory, draw calls:

```cpp
namespace uz::dev {

struct BenchmarkResults {
    float avg_frame_time_ms = 0.0f;
    float min_frame_time_ms = FLT_MAX;
    float max_frame_time_ms = 0.0f;
    float std_dev = 0.0f;

    size_t peak_memory_mb = 0;
    int draw_calls = 0;
    int triangles_rendered = 0;
};

/// Run benchmark for N frames
BenchmarkResults RunBenchmark(const char* name, int frame_count = 300);

}  // namespace uz::dev
```

### Usage

```cpp
uz::dev::RegisterTest("benchmark_scene_load", [](uz::World& world) {
    auto results = uz::dev::RunBenchmark("scene_load", 300);

    // Check performance thresholds
    if (results.avg_frame_time_ms > 16.67f) {  // 60 FPS = 16.67ms
        UZ_LOG(Test, Warning, "Scene too slow: {} ms/frame", results.avg_frame_time_ms);
        return false;  // Test fails if performance regresses
    }

    return true;
});
```

## CI Integration

Run tests in CI pipeline:

```bash
#!/bin/bash
# .github/workflows/tests.yml

game.exe --headless --run-tests --output test_results.json

if [ $? -ne 0 ]; then
    echo "Tests failed!"
    exit 1
fi

# Parse results
python3 parse_test_results.py test_results.json
```

## Test Output

Tests produce JSON report:

```json
{
    "timestamp": "2026-03-12T10:30:00Z",
    "total": 42,
    "passed": 40,
    "failed": 2,
    "skipped": 0,
    "duration_ms": 1234.5,
    "tests": [
        {
            "name": "physics_raycast",
            "status": "passed",
            "duration_ms": 12.3
        },
        {
            "name": "animation_playback",
            "status": "passed",
            "duration_ms": 45.6
        },
        {
            "name": "behavior_tree_sequence",
            "status": "failed",
            "error": "Expected Success, got Failure",
            "duration_ms": 89.2
        }
    ]
}
```

## Debugging Failed Tests

Enable verbose logging:

```cpp
uz::dev::RunAllTests(/* verbose = */ true);

// Output:
// [TEST] physics_raycast ... PASS (12.3ms)
// [TEST] animation_playback ... PASS (45.6ms)
// [TEST] behavior_tree_sequence ... FAIL
//   Error: Expected Success, got Failure
//   Blackboard:
//     has_player_target: false
//     position: (0, 0, 0)
```

## Comparison

| Feature | UE5 Automation | Google Test | Unity Test Framework | **UzEngine** |
|---------|---|---|---|---|
| Headless | Yes | N/A | Yes | Yes |
| Test registration | Automatic | Manual (TEST macro) | NUnit attributes | RegisterTest() |
| Fixtures | Yes (automation context) | Yes (TEST_F) | Yes ([SetUp]) | Manual (setup in lambda) |
| Screenshots | Yes (automation) | No | Limited | Yes (capture + compare) |
| Replay system | Limited | No | No | Yes (input recording) |
| Benchmarking | Yes (FPS trace) | Yes (timing) | Limited | Yes (frame time + memory) |
| CI integration | Yes | Yes | Yes | Yes (JSON output) |

## Implementation Phase

- **Phase 4**: Test framework, headless mode, screenshot capture, test reporting
- **Phase 5**: Replay system, benchmark suite, CI integration, visual regression detection
- **Phase 6**: Performance alerting (regression detection), distributed testing (cloud runners)
