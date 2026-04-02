## Phase 4e-4.24.0 — VFX Niagara-Style Module Stack UI

Reorganize the VFX editor UI to match Niagara's stage-based module stack.
Replace the flat parameter list with a hierarchical tree of collapsible stages,
each containing toggleable modules with inline parameters.

Reference: Niagara Emitter Editor — Emitter Spawn/Update, Particle Spawn/Update, Render.
See docs/analysis/niagara-module-stack-ui-architecture.md for full pattern analysis.

## 1. Data Model — VfxModule + VfxStage

- [x] 1.1 Create `VfxStage` enum: EmitterSpawn, EmitterUpdate, ParticleSpawn, ParticleUpdate, Render. Added as `VFXEditorUI::VfxStage` enum class in vfx_editor_ui.h.
- [x] 1.2 Create `VfxModuleDef` struct with name, stage, strengthPtr, enablePtr, savedStrength/savedEnable, vector of VfxModuleParam. IsActive() and FormatSummary() helper methods. All 30+ module types covered via BuildModuleList().
- [x] 1.3 Each VfxModuleDef has `std::vector<VfxModuleParam>` with typed pointers (Float, Uint, Bool, Enum) into VFXEmitterConfig fields. Includes min/max/enumNames/enumCount.
- [x] 1.4 BuildModuleList() maps all VFXEmitterConfig field groups to ~30 modules across 5 stages. Includes: BurstConfig, EmitterLifecycle, InitializeParticle, ShapeLocation, InitVelocity, InitRotation, VelocityInherit, Gravity, Damping, Drag, CurlNoise, VortexForce, Wind, PointForce, VectorField, Orbit, GroundCollision, TerrainCollision, RotationRate, Acceleration, DeathSpawn, Billboard, SizeOverLife, ColorOverLife, SizeBySpeed, ColorBySpeed, Emissive, SoftFade, LitMode, BlendMode, ScreenSpaceSize, ParticleLight.

## 2. Rebuild Left Panel as Stage Tree

- [x] 2.1 Replaced flat 7-category layout with 5 collapsible stages (EmitterSpawn, EmitterUpdate, ParticleSpawn, ParticleUpdate, Render). BuildStageTree() renders all ~30 modules grouped by stage. Old BuildLeftPanel() removed entirely.
- [x] 2.2 Stage headers: dark background (kColorStageHdr), click to expand/collapse. Unicode ▼/▶ prefix. Hover highlight. Per-stage expand state in m_stageExpanded[5].
- [x] 2.3 Module rows: 14x14 checkbox (green kColorCheckOn when active, gray kColorCheckOff when inactive) + module name + summary value (FormatSummary). Checkbox click toggles enable: saves/restores strengthPtr or enablePtr with savedStrength/savedEnable.
- [x] 2.4 Click module name area → expand to show params (accordion: only one expanded at a time via m_expandedModuleIndex).
- [x] 2.5 Expanded module shows full parameter sliders/toggles/dropdowns, indented by kModuleIndent (12px). Supports Float (Slider), Uint (Slider + cast), Bool (Toggle), Enum (DropdownTrigger).

## 3. Module Enable/Disable Logic

- [x] 3.1 Each module has strengthPtr (float) and/or enablePtr (uint32), with savedStrength/savedEnable for restore. Toggle logic: active→inactive saves and zeros; inactive→active restores (defaults to 1.0f/1u if saved was zero).
- [x] 3.2 All mapping implemented in BuildModuleList(): Gravity (no toggle), Damping (strengthPtr=damping), Drag (strengthPtr=dragCoefficient), CurlNoise (strengthPtr=curlStrength), Vortex (strengthPtr=vortexTangentialStrength), Wind (strengthPtr=windStrength), PointForce (enablePtr=pointForceEnabled, strengthPtr=pointForceStrength), VectorField (enablePtr=vectorFieldEnabled, strengthPtr=vectorFieldStrength), Orbit (enablePtr=orbitEnabled), GroundCollision (enablePtr=groundCollision), TerrainCollision (enablePtr=terrainCollisionEnabled), RotationRate (strengthPtr=rotationRate), SizeOverLife/ColorOverLife (always on), SizeBySpeed (enablePtr=sizeBySpeedEnabled), ColorBySpeed (enablePtr=colorBySpeedEnabled), DeathSpawn (enablePtr=deathSpawnEnabled), ParticleLight (enablePtr=particleLightEnabled), LitMode (enablePtr=litMode), Emissive (strengthPtr=emissiveIntensity), VelocityInherit (strengthPtr=velocityInheritance).
- [x] 3.3 Active indicator: green checkbox fill (kColorCheckOn) + inner white rect when active; gray border (kColorCheckOff) when inactive. IsActive() checks enablePtr first, then strengthPtr > 0, then defaults to true.

## 4. Remove Old Module Stack Panel

- [x] 4.1 Old BuildLeftPanel (flat 7-category accordion) replaced entirely by BuildStageTree. No separate VFXModuleStackPanel existed — the old categories were inline in BuildLeftPanel.
- [x] 4.2 Right panel repurposed as module detail panel (see section 5).
- [x] 4.3 No Q key handler existed for module stack. Old category state (m_expandedCategory) superseded by m_stageExpanded[] and m_expandedModuleIndex.

## 5. Keep Right Panel for Quick Module Sliders

- [x] 5.1 Right panel shows "Select a module" when no module expanded. When a module is expanded in the left tree, right panel shows the module name in title bar, active/inactive status indicator, and all params as full-width labeled sliders (same Slider/Toggle/DropdownTrigger widgets).
- [x] 5.2 Dual view works: left = stage tree overview with compact module rows, right = detail editing with large labeled sliders for the selected module.

## 6. Visual Polish

- [x] 6.1 Stage headers: kColorStageHdr (0.10,0.10,0.14) darker than module rows (0.16,0.16,0.21). Hover highlight via kColorBtnHover.
- [x] 6.2 Module checkbox: kColorCheckOn (green 0.25,0.65,0.35) when active, kColorCheckOff (gray 0.35,0.35,0.40) when inactive. Inner white rect checkmark.
- [x] 6.3 Disabled modules: dim text via kColorTextDim (0.50,0.50,0.55). Parameters only shown when module is expanded (click name to expand).
- [x] 6.4 Expanded module: kColorModuleExp (0.19,0.22,0.30) highlight bg. Unicode ▼ accent-colored indicator on right of row.
- [x] 6.5 FormatSummary() shows first param value (float "%.2f", uint "%u", bool "On/Off", enum name) in kColorSummary (0.65,0.70,0.80) right-aligned on module row.

## 7. Integration

- [x] 7.1 BuildStageTree() replaces BuildLeftPanel() in VFXEditorUI. BuildUI() calls BuildStageTree() when left panel is open. BuildModuleList() called on first frame and when config pointer changes (emitter switch).
- [x] 7.2 Same TextRenderer AddRect+AddText pattern. Same Slider/Toggle/DropdownTrigger widgets. Same theme color system extended with 7 new colors (kColorStageHdr, kColorModuleRow, kColorModuleExp, kColorCheckOn, kColorCheckOff, kColorTextDim, kColorSummary).
- [x] 7.3 Data binding: VfxModuleDef stores typed pointers (fPtr/uPtr) into VFXEmitterConfig fields. Rebuilt on emitter switch via m_lastConfig tracking.
- [x] 7.4 Scrollable: m_leftPanelScroll offset, kScrollSpeed=30px per notch, m_scrollDelta read from InputSystem::GetScrollDelta() in HandleMouse. Two-pass rendering: first pass computes total content height for scroll clamping, second pass renders with offset. Content clipped to visible area (contentTop/contentBot bounds).
