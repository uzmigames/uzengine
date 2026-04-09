# Material Format — Spec Delta

## ADDED Requirements

### Requirement: .uzmat File Format
The engine MUST support a human-readable .uzmat material file format containing shader reference, PBR scalar parameters, and texture map paths, parseable by both the engine and LLM agents.

#### Scenario: Parse Valid .uzmat
Given a .uzmat file with baseColor=[0.8,0.2,0.1], metallic=0.9, roughness=0.3, albedo_map="textures/rust.png"
When the MaterialManager loads it
Then a PBRMaterial SHALL be created with matching parameters
And the albedo texture SHALL be loaded and assigned an SRV index

#### Scenario: Malformed .uzmat
Given a .uzmat file with invalid syntax
When the MaterialManager attempts to load it
Then loading SHALL fail with a descriptive error message
And the default_lit material SHALL be used as fallback

### Requirement: Material Manager Caching
The MaterialManager MUST cache loaded materials by file path, returning the same instance for repeated loads and evicting when reference count reaches zero.

#### Scenario: Cache Hit
Given material "metals/gold.uzmat" is already loaded
When a second entity requests the same material path
Then the MaterialManager SHALL return the cached instance (no disk read)

#### Scenario: Cache Eviction
Given a material with reference count 1
When the last reference is released
Then the material SHALL be evicted from cache and GPU resources freed

### Requirement: Material Hot-Reload
The MaterialManager MUST detect .uzmat file changes via FileWatcher and reload the material without engine restart, updating all GPU-side data for entities using that material.

#### Scenario: Live Edit
Given a material "stone.uzmat" is loaded and rendered
When the user modifies roughness from 0.3 to 0.8 on disk
Then the FileWatcher SHALL detect the change
And the material SHALL be reloaded with roughness=0.8
And all objects using "stone.uzmat" SHALL render with updated roughness

### Requirement: Texture Path Resolution
The material system MUST resolve texture paths relative to the .uzmat file location first, then fall back to project root resolution.

#### Scenario: Relative Path
Given a .uzmat at "materials/stone.uzmat" referencing "textures/albedo.png"
When textures are resolved
Then the engine SHALL look for "materials/textures/albedo.png" first
And fall back to "textures/albedo.png" at project root
