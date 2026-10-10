## ADDED Requirements

### Requirement: Reproducible build and quality commands
The project SHALL build and run its declared quality gates from a clean checkout
using tracked dependency inputs and one documented C++23/toolchain contract.

#### Scenario: Clean dependency bootstrap (F-013)
Given a disposable checkout without inherited dependencies or build outputs
When the documented bootstrap and CI-equivalent commands run
Then runtime, DLL, native addon, extension and MCP use declared dependencies and report their real outcomes.

#### Scenario: Location-independent quality scripts (F-016)
Given a checkout path containing spaces and a caller outside the repository
When a supported PowerShell quality command runs
Then it discovers the repository, inspects the intended engine sources and propagates a controlled failure as non-success.

#### Scenario: Consistent language and compiler contract (F-017)
Given each supported runtime and native-consumer build path
When its configured language standard and toolchain are inspected and built
Then they satisfy the documented project contract or expose a concrete unresolved blocker without claiming support.

### Requirement: Honest test and reference outcomes
Required checks MUST NOT report success for absent tests, absent required fixtures,
unavailable coverage or missing, incompatible or below-threshold visual evidence.
Applicable Falcor comparisons SHALL require PSNR >= 35 dB with unchanged references.

#### Scenario: Empty or incomplete required test suite (F-014)
Given a required suite with zero discovered tests or a missing required database
When the test gate runs
Then it fails explicitly; optional hardware cases are reported separately as skipped and do not satisfy a required gate.

#### Scenario: Working coverage and lint gates (F-014)
Given the documented test dependencies and configured coverage scope
When coverage and lint run
Then the provider is available, collected tests are nonempty, the configured threshold is enforced and actual lint failures block acceptance.

#### Scenario: Required Falcor evidence (F-015)
Given required image pairs and their capture metadata
When any pair is missing, dimensionally incompatible or below 35 dB
Then the comparison returns a non-success result with a reason instead of approving a warning, skip or silently resized image.

#### Scenario: Independent reference claims (F-006)
Given an internal tracer sharing material or geometry helpers with the tested renderer
When its output is used as supporting evidence
Then the report states its supported scope and shared assumptions and does not substitute that agreement for the independent Falcor gate.

### Requirement: Bounded and valid entity lifetimes
The ECS SHALL separate entity slots from version bits for bounded bookkeeping
and MUST reject stale handles and cyclic hierarchy mutations.

#### Scenario: Repeated entity reuse (F-009)
Given repeated create, destroy and clear operations over a bounded set of slots
When entity versions advance
Then generation storage remains proportional to allocated slots and stale handles remain invalid without breaking valid handle conversion.

#### Scenario: Cyclic parenting (F-012)
Given a valid hierarchy accessed through native, Lua or scene-loading APIs
When self-parenting or an ancestor cycle is requested
Then the shared mutation boundary rejects it without changing the prior hierarchy.

### Requirement: Transactional scene-load results
Scene loading SHALL distinguish valid empty scenes from errors and SHALL preserve
consistent live scene, material and CVar state when validation fails.

#### Scenario: Invalid and empty scene inputs (F-011)
Given a live scene and a candidate input containing malformed data, invalid field types or a valid empty scene
When the loader processes that candidate
Then it reports a structured outcome and commits only validated state; failed inputs preserve the prior live state.

### Requirement: Explicit editor ownership and bounded communication
World mutation SHALL obey one documented ownership protocol, and native/editor
queues and pending requests SHALL have bounded, observable lifetimes.

#### Scenario: Commands during rendering (F-010)
Given an active render loop
When scene loads, property changes, resize and shutdown requests overlap
Then world access follows the ownership protocol and every request completes or fails explicitly without unsynchronized mutation.

#### Scenario: Slow consumer or failed worker (F-020)
Given a stalled viewport consumer or a worker that terminates with pending requests
When frames and commands are produced
Then queue depth and memory remain bounded, requests resolve or fail within documented deadlines, and encoded-stream recovery respects H.264 dependencies.

### Requirement: Valid temporal inputs and outputs
Temporal rendering SHALL distinguish matrix validity, resource state and content
validity, and SHALL consume only successfully produced output with valid motion inputs.

#### Scenario: History lifecycle transitions (F-001)
Given startup, resize, camera cut, scene change or a TAA/upscaler switch
When temporal rendering resumes
Then barriers use actual resource states and invalid or unrelated history is reset before it can affect the output.

#### Scenario: Object motion with a fixed camera (F-002)
Given rigid, skinned and supported vertex-deformed meshes moving at a stationary camera
When velocity is generated
Then it represents their previous-to-current projected displacement using the documented jitter and resolution convention.

#### Scenario: Unavailable or failed upscaler (F-005)
Given an unavailable SDK, failed initialization or failed output production
When that upscaler is selected or executed
Then the system rejects selection or produces a valid documented fallback and does not present unwritten or stale history as current output.

### Requirement: Complete graph synchronization
The render graph SHALL express required ordered accesses independently of state
changes and SHALL preserve correct state knowledge across resource lifetimes.

#### Scenario: Dependent UAV accesses (F-003)
Given dependent passes using a resource in UAV state
When no state transition is needed but access ordering is required
Then the graph emits the required synchronization and a negative-control test detects its omission.

#### Scenario: Real dependencies and pooled resource reuse (F-003)
Given imported, newly created or reused transient resources
When passes access them across frames or recreation
Then declared edges identify the actual accessed resources and barriers agree with actual lifetime and final-state information.

### Requirement: Accurate material input and geometry contracts
Generated materials SHALL receive real frame/object/vertex context, and glass
models SHALL expose and obey their supported geometric assumptions.

#### Scenario: Material graph context (F-004)
Given diagnostic graph materials consuming time, screen coordinates, positions and vertex color
When camera, object, time or viewport dimensions change
Then the compiled material draw consumes the corresponding real inputs at each tested resolution.

#### Scenario: Glass geometry support (F-007)
Given sphere, pane and nonuniformly scaled glass fixtures
When they are authored and rendered
Then supported cases satisfy the declared model and unsupported geometry is not represented as validated general-volume transmission.

### Requirement: Explicit and portable capabilities
Capability documentation and selection SHALL distinguish implemented, integrated
and validated behavior, and runtime discovery MUST remain path-agnostic.

#### Scenario: Inactive post-process effects (F-008)
Given controls for effects with unfinished or undispatched implementations
When capability status is inspected or the controls are used
Then their actual availability is explicit and no unfinished pass is wired into the active graph as a completed feature.

#### Scenario: Non-default operating-system installation (F-021)
Given a supported system whose Windows directory is not the hardcoded default
When DLSS discovery runs
Then it uses supported discovery or explicit configuration and reports capability availability without relying on an absolute assumed path.

#### Scenario: Layout and capability documentation (F-022)
Given a change to shared CPU/HLSL layout or a renderer responsibility boundary
When the change is validated and documented
Then layout compatibility is checked, accepted behavior is retained, and capability claims identify the tested revision, backend and remaining limitations.

### Requirement: Measured performance changes
Optimization work SHALL preserve accepted correctness and support performance
claims with comparable measurements.

#### Scenario: Instance batching and instanced draws (F-018)
Given single-mesh and mixed-mesh/material scenes with a validated per-draw baseline
When batching preparation or instanced drawing changes
Then geometry, transforms, materials, shadows and culling remain correct and the report includes preparation cost, draw counts and CPU/GPU timings.

#### Scenario: Concurrent loose-file reads (F-019)
Given representative asset sizes and stated cache/storage conditions
When loose-file reads run serially and concurrently after any lock-scope change
Then contents and mount lifetimes remain correct and measured throughput supports any claimed improvement.
