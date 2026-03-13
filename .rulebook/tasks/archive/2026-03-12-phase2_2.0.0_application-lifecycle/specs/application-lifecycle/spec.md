# Application Lifecycle — Spec Delta

## ADDED Requirements

### Requirement: ISubsystem Interface
The engine SHALL define an `ISubsystem` interface with `Init()`, `Shutdown()`, `Tick(float dt)`, `GetName()`, and `GetDependencies()` methods. All engine subsystems MUST implement this interface to participate in the lifecycle.

#### Scenario: Subsystem implements interface
Given a class that implements ISubsystem
When it is registered with SubsystemRegistry
Then its Init() is called during the appropriate lifecycle phase
And its Shutdown() is called during reverse-order teardown

### Requirement: SubsystemRegistry Dependency Ordering
The SubsystemRegistry SHALL topologically sort subsystems based on declared dependencies. Init MUST proceed in dependency order. Shutdown MUST proceed in strict reverse of init order.

#### Scenario: Subsystems with dependencies init in correct order
Given subsystem B declares dependency on subsystem A
When the engine initializes
Then subsystem A Init() is called before subsystem B Init()

#### Scenario: Circular dependency is detected
Given subsystem A depends on B and subsystem B depends on A
When both are registered
Then the registry SHALL report an error and refuse to initialize

#### Scenario: Shutdown in reverse order
Given subsystems A, B, C were initialized in that order
When the engine shuts down
Then Shutdown() is called in order C, B, A

### Requirement: Three-Phase Initialization
The EngineLoop SHALL execute initialization in three ordered phases: PreInit (CPU-only: logging, config, paths, VFS), Init (GPU: window, RHI, renderer), and Start (world: scene loading, entity spawning). No GPU calls SHALL occur during PreInit.

#### Scenario: PreInit completes before Init
Given the engine is starting up
When EngineLoop runs initialization
Then all PreInit subsystems complete before any Init subsystem begins
And all Init subsystems complete before any Start subsystem begins

#### Scenario: No GPU calls during PreInit
Given a subsystem registered in PreInit phase
When it calls any RHI function during Init()
Then an assertion SHALL fire indicating GPU is not available

### Requirement: Fixed-Timestep Main Loop
The EngineLoop main loop SHALL use a fixed-timestep accumulator for physics ticks and variable-rate rendering. The fixed timestep MUST be configurable (default 1/60s). Multiple physics ticks SHALL execute per frame when frame time exceeds the fixed step.

#### Scenario: Physics ticks at fixed rate
Given fixed timestep is 1/60s and frame time is 1/30s
When the main loop processes one frame
Then exactly 2 physics ticks are executed
And one render pass is executed

#### Scenario: Delta time clamping
Given the engine resumes after a debugger pause of 5 seconds
When the frame delta is computed
Then it SHALL be clamped to a maximum value (e.g., 0.25s) to prevent physics explosion

### Requirement: TimeManager
The engine SHALL provide a TimeManager subsystem exposing `deltaTime`, `totalTime`, `fixedDeltaTime`, `frameCount`, and `timeScale`. TimeManager MUST be updated once per frame before any tick group executes.

#### Scenario: Frame timing is accurate
Given the engine is running
When a new frame begins
Then deltaTime reflects wall-clock time since last frame
And frameCount increments by exactly 1
And totalTime increases by deltaTime

### Requirement: Tick Groups
The EngineLoop SHALL support ordered tick groups: PrePhysics, PostPhysics, Update, PostUpdate. Subsystems MUST register into one or more tick groups. Tick groups SHALL execute in the defined order every frame.

#### Scenario: Tick group execution order
Given subsystem A is in PrePhysics and subsystem B is in PostPhysics
When one frame tick executes
Then A.Tick() is called before B.Tick()

### Requirement: Module Self-Registration
The engine SHALL provide a `UZ_REGISTER_MODULE()` macro that enables static self-registration of modules at startup. Duplicate module names SHALL be detected and reported as errors.

#### Scenario: Module registers at startup
Given a translation unit contains UZ_REGISTER_MODULE(MyModule)
When the engine starts
Then MyModule's subsystems are automatically registered without manual wiring

### Requirement: CLI Argument Parsing
The EngineLoop SHALL parse command-line arguments before PreInit. Supported flags MUST include `--headless` (no window/GPU), `--screenshot N` (capture frame N and exit), and `--config path` (override config file). CLI args SHALL override `.uzproject` config values.

#### Scenario: Headless mode skips GPU
Given the engine is launched with --headless
When initialization runs
Then no window is created and no RHI device is initialized
And the engine ticks without rendering

### Requirement: Graceful Shutdown
The EngineLoop SHALL execute shutdown in strict reverse order of initialization. GPU fences MUST be waited on before destroying GPU resources. Shutdown MUST result in zero memory leaks and zero crashes.

#### Scenario: GPU resources cleaned up
Given the engine is running with active GPU resources
When shutdown is triggered
Then all GPU fences are waited on before resource destruction
And no D3D12 debug layer warnings are emitted
