## Phase 4e-4.24.0 — VFX Niagara-Style Module Stack UI

Reorganize the VFX editor UI to match Niagara's stage-based module stack.
Replace the flat parameter list with a hierarchical tree of collapsible stages,
each containing toggleable modules with inline parameters.

Reference: Niagara Emitter Editor — Emitter Spawn/Update, Particle Spawn/Update, Render.
See docs/analysis/niagara-module-stack-ui-architecture.md for full pattern analysis.

## 1. Data Model — VfxModule + VfxStage

- [ ] 1.1 Create `VfxStage` enum: EmitterSpawn, EmitterUpdate, ParticleSpawn, ParticleUpdate, Render.
- [ ] 1.2 Create `VfxModule` struct: `{ const char* name; VfxStage stage; bool enabled; VfxModuleType type; }`.
      Types: SpawnRate, BurstConfig, InitPosition, InitVelocity, ShapeLocation, Gravity, Damping,
      Drag, CurlNoise, Vortex, Wind, PointForce, VectorField, Orbit, GroundCollision,
      TerrainCollision, KillVolume, SizeOverLife, ColorOverLife, ColorRamp, SizeBySpeed,
      ColorBySpeed, RotationRate, VelocityInheritance, Acceleration, BillboardRenderer,
      MeshRenderer, LitMode, SoftFade, Emissive, DeathSpawn.
- [ ] 1.3 Each VfxModule has a `std::vector<VfxModuleParam>` — the editable parameters for that module.
      VfxModuleParam: `{ const char* name; enum Type {Float, Uint, Bool, Enum}; void* valuePtr; float min, max, step; }`.
- [ ] 1.4 Build the module list from VFXEmitterConfig — map each config field group to a module.
      Example: gravity[3] + damping → separate Gravity module and Damping module.

## 2. Rebuild Left Panel as Stage Tree

- [ ] 2.1 Replace the current flat category list with 5 collapsible STAGES:
      ```
      ▼ Emitter Spawn
        [✓] Burst Config         Rate: 300  Count: 500
      ▼ Emitter Update  
        [✓] Emitter Lifecycle    Duration: 0  Loops: 0
      ▼ Particle Spawn
        [✓] Initialize Particle  Size: 1.0  Life: 1.0-2.5
        [✓] Shape Location       Shape: Sphere  Radius: 0.2
        [✓] Init Velocity        VelZ: 6.0  Spread: 1.0
        [✓] Velocity Inherit     Factor: 0.0
      ▼ Particle Update
        [✓] Gravity              Z: -3.0
        [✓] Damping              0.15
        [○] Drag                 0.0
        [○] Curl Noise           Str: 0.0
        [○] Vortex Force         Str: 0.0
        [○] Wind                 Str: 0.0
        [○] Point Force          Str: 0.0
        [○] Vector Field         Str: 0.0
        [✓] Ground Collision     Mode: Bounce
        [○] Terrain Collision    Off
        [○] Kill Volume          Off
        [✓] Rotation Rate        0.0
        [✓] Acceleration         XYZ: 0,0,0
        [○] Orbit                Off
        [○] Death Spawn          Off
      ▼ Render
        [✓] Billboard            Facing: Camera  Stretch: 0.0
        [✓] Size Over Life       0.5→1.2→0.0
        [✓] Color Over Life      Start→End
        [○] Color Ramp           Off
        [○] Size by Speed        Off
        [○] Color by Speed       Off
        [✓] Emissive             1.0
        [✓] Soft Fade            0.5
        [✓] Lit Mode             Off
        [○] Particle Light       Off
      ```
- [ ] 2.2 Stage headers: dark background, click to expand/collapse. Show "▼"/"▶" prefix.
- [ ] 2.3 Module rows: checkbox [✓]/[○] on left, module name, key parameter summary on right.
      Click checkbox → toggle module.enabled. When disabled, set the module's strength/enable to 0.
      When re-enabled, restore the previous saved value.
- [ ] 2.4 Click module NAME (not checkbox) → expand to show all parameters as sliders below.
      Only one module expanded at a time (accordion behavior).
- [ ] 2.5 Expanded module shows full parameter sliders (same Slider widget as current).
      Indent parameters under the module row.

## 3. Module Enable/Disable Logic

- [ ] 3.1 Each module maps to one or more VFXEmitterConfig fields.
      When disabled: save current values, set to zero/off.
      When re-enabled: restore saved values.
- [ ] 3.2 Mapping table (module → config field(s)):
      - Gravity → config.gravity[2] (Z only, or all 3)
      - Damping → config.damping
      - Drag → config.dragCoefficient
      - Curl Noise → config.curlStrength
      - Vortex → config.vortexTangentialStrength + config.vortexRadialStrength
      - Wind → config.windStrength
      - Point Force → config.pointForceStrength + config.pointForceEnabled
      - Vector Field → config.vectorFieldStrength + config.vectorFieldEnabled
      - Orbit → config.orbitEnabled + config.orbitRadius
      - Ground Collision → config.groundCollision
      - Terrain Collision → config.terrainCollisionEnabled
      - Kill Volume → killVolumeEnabled (if exists)
      - Rotation Rate → config.rotationRate
      - Size Over Life → always on (sizeOverLifeStart/Peak/End)
      - Color Over Life → always on (colorLifeStart/End)
      - Color Ramp → colorRampLutSrvIndex != UINT32_MAX
      - Size by Speed → config.sizeBySpeedEnabled
      - Color by Speed → config.colorBySpeedEnabled
      - Death Spawn → config.deathSpawnEnabled
      - Particle Light → config.particleLightEnabled
      - Lit Mode → config.litMode
- [ ] 3.3 Active indicator: green [✓] when value > 0 or enabled=1, gray [○] when zero/disabled.

## 4. Remove Old Module Stack Panel

- [ ] 4.1 Remove the old right-side VFXModuleStackPanel (Q key, vfx_module_stack.h/cpp).
      The new left panel stage tree replaces it entirely.
- [ ] 4.2 The right panel area becomes available for future use (preview, curves, etc.).
- [ ] 4.3 Remove Q key handler and module stack rendering from demo.

## 5. Keep Right Panel for Quick Module Sliders

- [ ] 5.1 Actually, keep the right panel but repurpose: show ONLY the currently expanded module's
      parameters as large, easy-to-drag sliders. When no module is expanded, show "Select a module".
- [ ] 5.2 This gives dual view: left = tree overview, right = detail editing.

## 6. Visual Polish

- [ ] 6.1 Stage headers: slightly brighter than module rows. Bold text.
- [ ] 6.2 Module checkbox: accent color when enabled, gray when disabled.
- [ ] 6.3 Disabled modules: dim text (0.5 alpha). Parameters hidden.
- [ ] 6.4 Expanded module: highlight background. Show small "▼" indicator.
- [ ] 6.5 Parameter summary on module row: show 1-2 key values in small text (e.g., "Z:-3.0").

## 7. Integration

- [ ] 7.1 VfxStackUI class replaces the current BuildLeftPanel in VFXEditorUI.
- [ ] 7.2 Same TextRenderer AddRect+AddText pattern. Same Slider/Toggle/Dropdown widgets.
- [ ] 7.3 Data binding: VfxModule stores pointers to VFXEmitterConfig fields (same as current).
- [ ] 7.4 Scrollable if content exceeds panel height (simple offset-based scroll on mouse wheel).
