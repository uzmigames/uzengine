# VFX Graph Asset Format — Specification

## ADDED Requirements

### Requirement: VFX Graph Loading
The VFX system SHALL load .uzvfx JSON files and instantiate a complete emitter configuration with modules, parameters, and events.

#### Scenario: Load Valid .uzvfx File
Given a .uzvfx JSON file with format="uzvfx", version=1, 3 modules, 2 parameters, 1 event
When VFXGraph::Load(path) is called
Then a VFXGraph object SHALL be returned with 3 module instances
And modules SHALL be instantiated via ModuleRegistry in the order specified
And 2 parameters SHALL be parsed with correct types and default values
And 1 event SHALL be parsed with name and emission_count

#### Scenario: Load Invalid .uzvfx File
Given a .uzvfx JSON file with an unknown module type "custom_plasma"
When VFXGraph::Load(path) is called
Then loading SHALL fail gracefully
And UZ_LOG(VFX, Error) SHALL report the unknown module type
And nullptr SHALL be returned

### Requirement: Runtime Parameter Modification
The VFX parameter system SHALL allow runtime modification of emitter parameters without asset reload or shader recompilation.

#### Scenario: Set Parameter at Runtime
Given an active ParticleEmitter with parameter "intensity" (default=1.0)
When SetParameter("intensity", 2.0) is called
Then the next frame's compute dispatch SHALL use intensity=2.0
And no shader recompilation SHALL occur
And no asset reload SHALL occur

#### Scenario: Parameter GPU Upload
Given a VFXParameterStore with 5 parameters
When UploadToGPU(cbAllocator) is called
Then a constant buffer allocation SHALL be created
And all 5 parameters SHALL be packed as Vec4 values (padding as needed)
And the CB GPU address SHALL be bindable to compute shader register b1

### Requirement: VFX Event System
The VFX system SHALL support named events that trigger one-shot particle bursts.

#### Scenario: Burst Event Emission
Given an emitter with event "burst" (emission_count=500, velocity_scale=1.5)
When EmitEvent("burst") is called
Then 500 particles SHALL be spawned in the next frame
And spawn velocity SHALL be multiplied by 1.5
And normal continuous emission SHALL continue alongside the burst

#### Scenario: Unknown Event
Given an emitter without event "explode" defined
When EmitEvent("explode") is called
Then nothing SHALL happen
And UZ_LOG(VFX, Warning) SHALL report the unknown event name

### Requirement: Composite Effects
The VFX system SHALL support multi-emitter composite effects defined in .uzvfx_composite JSON files.

#### Scenario: Load Composite Effect
Given a .uzvfx_composite with 3 emitters (fire delay=0, smoke delay=0.2, debris delay=0)
When VFXComposite::Load(path) is called
Then 3 emitter configurations SHALL be parsed
And each SHALL reference a .uzvfx graph path, offset Vec3, and delay float

#### Scenario: Spawn Composite Effect
Given a loaded VFXComposite with fire (delay=0) and smoke (delay=0.2)
When Spawn(entity, position) is called
Then fire emitter SHALL start immediately
And smoke emitter SHALL start 0.2 seconds later
And both SHALL be parented to the same entity

### Requirement: VFX Hot-Reload
The VFX system SHALL support hot-reload of .uzvfx files in development builds, updating active emitters without restart.

#### Scenario: Hot-Reload on File Change
Given an active emitter using "content/vfx/fire.uzvfx" in a UZ_DEV_BUILD
When the .uzvfx file is modified and saved to disk
Then FileWatcher SHALL detect the change
And VFXGraph SHALL be reloaded from disk
And compute shader SHALL be recompiled with new module configuration
And all active emitters using that graph SHALL swap to the new shader

### Requirement: Lua VFX API
The VFX system SHALL expose a Lua API for scripting particle effects.

#### Scenario: Lua Spawn and Control
Given a Lua script with access to ctx.vfx
When the script calls ctx.vfx:spawn("content/vfx/fire.uzvfx", {0, 5, 0})
Then a new ParticleEmitter SHALL be created at position [0,5,0]
And the returned entity handle SHALL support :set_parameter(), :emit_event(), :stop()

#### Scenario: Lua Parameter Modification
Given an active emitter entity in Lua
When emitter:set_parameter("color_tint", {1, 0.5, 0}) is called
Then the emitter's color_tint parameter SHALL be updated to [1, 0.5, 0]
And the change SHALL take effect on the next frame
