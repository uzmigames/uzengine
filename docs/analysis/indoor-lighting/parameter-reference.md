<!-- doc-meta
title: Indoor Lighting Parameters — Quick Reference for Three.js, Godot, and UzEngine
date: 2026-04-06
category: rendering
tags: lighting, parameters, reference, comparison
-->

# Indoor Lighting Parameters — Quick Reference

A side-by-side comparison of lighting parameters across Three.js, Godot 4, and UzEngine for implementing indoor lighting systems.

---

## Ambient Fill Lights

### Three.js AmbientLight

```javascript
new THREE.AmbientLight(color : number, intensity : number)
```

| Parameter | Default | Typical Indoor | Purpose |
|-----------|---------|----------------|---------|
| `color` | 0xffffff | 0xffffff | Light color (white = neutral) |
| `intensity` | 1.0 | 0.3–0.5 | Global brightness multiplier |

**Usage**: Provides uniform fill to prevent completely black areas. NOT directional.

**Recommendation**: Use with HemisphereLight. Keep intensity LOW (0.2–0.4) to preserve shadow depth.

### Three.js HemisphereLight

```javascript
new THREE.HemisphereLight(skyColor, groundColor, intensity)
```

| Parameter | Default | Typical Indoor | Purpose |
|-----------|---------|----------------|---------|
| `skyColor` | 0xffffff | 0xb1e1ff (light blue) | Light from above (ceiling) |
| `groundColor` | 0xffffff | 0xb97316 (warm brown) | Light from below (floor bounce) |
| `intensity` | 1.0 | 0.6–1.0 | Total contribution multiplier |

**Usage**: Directional fill based on surface normal. Mimics sky → ceiling and ground → floor bounce.

**Recommendation for interior**: Use cool sky color (0.7, 0.8, 1.0) and warm ground color (0.8, 0.7, 0.6) for realism.

### Godot 4 Environment (Ambient)

```gdscript
env.ambient_light_source = Environment.AMBIENT_LIGHT_DISABLED  # or ENVIRONMENT, COLOR
env.ambient_light_energy = 1.0
env.ambient_light_energy_source = 1.0
env.ambient_light_color = Color.WHITE
```

| Parameter | Default | Typical Indoor | Purpose |
|-----------|---------|----------------|---------|
| `ambient_light_source` | ENVIRONMENT | COLOR (for indoor) | Where ambient comes from |
| `ambient_light_energy` | 1.0 | 0.5–2.0 | Brightness multiplier |
| `ambient_light_color` | white | (1,1,1) | Color tint |

**Usage**: Uniform ambient contribution. In interior-only scenes, set source to COLOR instead of ENVIRONMENT to prevent sky leaking.

### UzEngine SkyLightComponent

```cpp
struct SkyLightComponent {
    float intensity = 1.0f;                    // Overall multiplier
    float indirectLightingIntensity = 1.0f;   // Diffuse fill specifically
    Vec3 lightColor = {1.0f, 1.0f, 1.0f};
    bool lowerHemisphereIsBlack = true;
    Vec3 lowerHemisphereColor = {0.0f, 0.0f, 0.0f};
};
```

| Parameter | Default | Typical Indoor | Purpose |
|-----------|---------|----------------|---------|
| `intensity` | 1.0 | 1.5–2.5 | Overall IBL multiplier |
| `indirectLightingIntensity` | 1.0 | 2.0–3.0 | Diffuse contribution ONLY |
| `lightColor` | (1,1,1) | (1,1,1) | Color tint |
| `lowerHemisphereIsBlack` | true | false (indoor) | Allow floor bounce |
| `lowerHemisphereColor` | (0,0,0) | (0.15, 0.2, 0.25) | Warm floor bounce |

**Usage**: Image-based ambient lighting from sky cubemap. For interior, boost `indirectLightingIntensity` and enable lower hemisphere.

**Recommendation**: For Sponza atrium, set `intensity=2.0`, `indirectLightingIntensity=2.5`, `lowerHemisphereColor=(0.15, 0.2, 0.25)`.

---

## Local Fill Lights (Point Lights)

### Three.js PointLight

```javascript
new THREE.PointLight(color, intensity, distance, decay)
```

| Parameter | Default | Typical Indoor | Purpose |
|-----------|---------|----------------|---------|
| `color` | 0xffffff | 0xffffff (white) | Light color |
| `intensity` | 100.0 | 800–1500 | Brightness (no standard unit) |
| `distance` | 0 (infinite) | 2000–5000 | Attenuation range (world units) |
| `decay` | 2.0 | 2.0 | Falloff curve (2.0 = physically correct) |

**Usage**: Omnidirectional light from a point. Ideal for ceiling lamps, wall sconces, fire.

**Example setup**:
```javascript
const ceilingLamp = new THREE.PointLight(0xffffff, 1200.0, 4000.0, 2.0);
ceilingLamp.position.set(100, 1200, -50);
scene.add(ceilingLamp);
```

### Three.js SpotLight

```javascript
new THREE.SpotLight(color, intensity, distance, angle, penumbra, decay)
```

| Parameter | Default | Typical Indoor | Purpose |
|-----------|---------|----------------|---------|
| `color` | 0xffffff | 0xffffff | Light color |
| `intensity` | 100.0 | 500–1000 | Brightness |
| `distance` | 0 (infinite) | 2000–4000 | Attenuation range |
| `angle` | π/3 (60°) | 45° (Math.PI/4) | Cone half-angle in radians |
| `penumbra` | 0.0 | 0.5 | Soft edge (0.0 = hard, 1.0 = full) |
| `decay` | 2.0 | 2.0 | Falloff curve |

**Usage**: Cone-shaped light. For indoor: ceiling lights, spotlights, street lamps.

**Example setup**:
```javascript
const spotlight = new THREE.SpotLight(0xffffff, 600.0, 3000.0, Math.PI/4, 0.5, 2.0);
spotlight.position.set(-100, 600, 200);
spotlight.target.position.set(-100, 0, 200);
scene.add(spotlight);
scene.add(spotlight.target);
```

### Godot 4 OmniLight3D (Point Light)

```gdscript
var light = OmniLight3D.new()
light.energy = 2.0                      # Brightness multiplier
light.omni_range = 3000.0               # Attenuation range (cm)
light.omni_attenuation = 2.0            # Falloff exponent
light.color = Color.WHITE
```

| Parameter | Default | Typical Indoor | Purpose |
|-----------|---------|----------------|---------|
| `energy` | 1.0 | 1.0–2.0 | Brightness multiplier |
| `omni_range` | 5.0 | 2000–4000 | Attenuation distance (cm) |
| `omni_attenuation` | 2.0 | 2.0 | Falloff curve (2.0 = inverse-square) |
| `color` | white | (1,1,1) | Light color |

**Usage**: Same as Three.js PointLight. Godot uses energy multiplier instead of raw intensity.

### Godot 4 SpotLight3D

```gdscript
var light = SpotLight3D.new()
light.energy = 1.0
light.omni_range = 2500.0
light.spot_angle = 45.0                 # Cone angle in degrees
light.spot_angle_attenuation = 1.0      # Soft edge falloff
light.color = Color.WHITE
```

| Parameter | Default | Typical Indoor | Purpose |
|-----------|---------|----------------|---------|
| `energy` | 1.0 | 0.8–1.5 | Brightness |
| `omni_range` | 5.0 | 2000–3500 | Attenuation distance (cm) |
| `spot_angle` | 45.0 | 30–60 | Cone angle in **degrees** |
| `spot_angle_attenuation` | 1.0 | 0.8–1.2 | Soft edge (0.0 = hard) |

**Usage**: Same as Three.js SpotLight. Angle is in degrees (more intuitive).

### UzEngine PointLightComponent

```cpp
struct PointLightComponent : LightComponent {
    float range = 1000.0f;                           // Attenuation radius (cm)
    bool useInverseSquaredFalloff = true;            // Physical falloff
    float lightFalloffExponent = 8.0f;               // If inverse-squared = false
    float sourceRadius = 0.0f;                       // Area light size
    bool castShadows = false;                        // Expensive for fill
};

struct LightComponent {  // Base class
    Vec3 color = {1.0f, 1.0f, 1.0f};
    float intensity = 10.0f;
    float specularScale = 1.0f;
    float diffuseScale = 1.0f;
};
```

| Parameter | Default | Typical Indoor | Purpose |
|-----------|---------|----------------|---------|
| `color` | white | (1,1,1) | Light color |
| `intensity` | 10.0 | 400–1200 | Brightness (lux/candela equivalent) |
| `range` | 1000 | 2500–4000 | Attenuation distance (cm) |
| `useInverseSquaredFalloff` | true | true | Physical inverse-square falloff |
| `specularScale` | 1.0 | 0.8–1.2 | Non-physical specular boost |
| `diffuseScale` | 1.0 | 1.0–1.5 | Non-physical diffuse boost |
| `castShadows` | false | false (fill) | Expensive, disable for fill lights |

**Usage**: Same as Three.js/Godot. UzEngine uses `intensity` in lux/candela units.

**Example setup (C++)**:
```cpp
auto light = world.CreateEntity();
auto& ptLight = world.AddComponent<PointLightComponent>(light);
ptLight.color = {1.0f, 0.95f, 0.9f};      // Warm white
ptLight.intensity = 800.0f;
ptLight.range = 3000.0f;
ptLight.castShadows = false;
```

### UzEngine SpotLightComponent

```cpp
struct SpotLightComponent : PointLightComponent {
    float innerAngleDeg = 30.0f;          // Full intensity cone
    float outerAngleDeg = 45.0f;          // Zero intensity beyond
    bool castShadows = false;
};
```

| Parameter | Default | Typical Indoor | Purpose |
|-----------|---------|----------------|---------|
| `innerAngleDeg` | 30.0 | 20–40 | Full intensity cone (degrees) |
| `outerAngleDeg` | 45.0 | 40–60 | Soft edge falloff (degrees) |
| `color` | white | (1,1,1) | Light color |
| `intensity` | 10.0 | 400–1000 | Brightness |
| `range` | 1000 | 2000–3500 | Attenuation distance (cm) |
| `castShadows` | false | false | Expensive, disable for fill |

**Usage**: Cone-shaped light. Inherits all PointLight properties.

**Example setup**:
```cpp
auto light = world.CreateEntity();
auto& spotLight = world.AddComponent<SpotLightComponent>(light);
spotLight.color = {1.0f, 1.0f, 0.95f};
spotLight.intensity = 600.0f;
spotLight.range = 3000.0f;
spotLight.innerAngleDeg = 30.0f;
spotLight.outerAngleDeg = 45.0f;
spotLight.castShadows = false;
```

---

## Real-Time GI Systems

### Godot 4 VoxelGI

```gdscript
var voxel_gi = VoxelGI.new()
voxel_gi.size = Vector3(6000, 3000, 6000)      # Scene extents (cm)
voxel_gi.interior = true                       # CRITICAL for interior scenes
voxel_gi.subdiv = VoxelGI.Subdiv.SUBDIV_64    # Voxel count (higher = detail)
voxel_gi.bias = 1.5                            # Light bleeding fix
voxel_gi.normal_bias = 2.0                     # Self-occlusion fix
```

| Parameter | Default | Typical Indoor | Purpose |
|-----------|---------|----------------|---------|
| `size` | (20, 20, 20) | (6000, 3000, 6000) | Voxel grid extents (cm) |
| `interior` | false | **true** | Prevent sky leaking |
| `subdiv` | 64 | 64–128 | Voxel count (more = detail) |
| `bias` | 1.0 | 1.5–2.0 | Reduce light bleeding |
| `normal_bias` | 2.0 | 2.0–3.0 | Reduce self-occlusion |

**Setup workflow**:
1. Create VoxelGI node
2. Set `interior = true`
3. Resize to match your level
4. Click "Bake" in the editor
5. Adjust `bias`/`normal_bias` if artifacts appear

### Godot 4 SDFGI

```gdscript
var env = Environment.new()
env.sdfgi_enabled = true
env.sdfgi_cascades = 4                         # Cascade count (1–8)
env.sdfgi_min_cell_size = 50.0                 # Detail level (cm)
env.sdfgi_y_scale = 0.5                        # 50% = 2x Y detail
env.sdfgi_read_sky_light = false               # CRITICAL for interior
env.sdfgi_use_occlusion = true
```

| Parameter | Default | Typical Indoor | Purpose |
|-----------|---------|----------------|---------|
| `sdfgi_enabled` | false | **true** | Enable SDFGI |
| `sdfgi_cascades` | 4 | 3–6 | Cascade count (more = farther) |
| `sdfgi_min_cell_size` | 50.0 | 25–50 | Detail level (lower = finer) |
| `sdfgi_y_scale` | 1.0 | 0.5 (for interiors) | Vertical scale (0.5 = 2x Y detail) |
| `sdfgi_read_sky_light` | true | **false** (interior only) | Read sky light? |
| `sdfgi_use_occlusion` | true | true | Use AO? |

**Setup workflow**:
1. Create WorldEnvironment node
2. Set `sdfgi_enabled = true`
3. Set `sdfgi_read_sky_light = false` for interior-only scenes
4. No baking needed (fully real-time)

---

## Common Indoor Lighting Scenarios

### Scenario 1: Atrium (Like Sponza)

Large interior space with high ceilings and exterior walls.

**Setup**:
- 1x DirectionalLight (sun, outside)
- 1x SkyLight (intensity=2.0, indirectLightingIntensity=2.5)
- 3–5x PointLight (ceiling, columns, floor)
- 1x GTAO postprocess (depth cues)

**Parameters (UzEngine)**:
```cpp
// SkyLight
skyLight.intensity = 2.0f;
skyLight.indirectLightingIntensity = 2.5f;
skyLight.lowerHemisphereIsBlack = false;
skyLight.lowerHemisphereColor = {0.15f, 0.2f, 0.25f};

// Ceiling light
PointLightComponent ptLight;
ptLight.color = {1.0f, 1.0f, 1.0f};
ptLight.intensity = 1200.0f;
ptLight.range = 4000.0f;
```

### Scenario 2: Underground Cavern / Basement

Fully enclosed, no exterior light.

**Setup**:
- 1x SkyLight (minimal, intensity=0.3) OR disabled
- 5–8x PointLight (scattered throughout)
- 1x SpotLight (torch/emergency light)
- Optional: VoxelGI or SDFGI with `interior=true`

**Parameters (Godot)**:
```gdscript
# If using SkyLight:
sky_light.energy = 0.3
sky_light.ambient_light_energy = 0.5

# If using SDFGI:
env.sdfgi_enabled = true
env.sdfgi_read_sky_light = false
env.sdfgi_y_scale = 0.5
```

### Scenario 3: House Interior (Small Rooms)

Multiple small rooms with natural windows.

**Setup**:
- 1x DirectionalLight (sun through windows)
- 1x HemisphereLight (outdoor sky gradient)
- 1x AmbientLight (low, for minimum brightness)
- 2–3x PointLight per room (ceiling fixture)
- 1x SpotLight per window (sunlight simulation)

**Parameters (Three.js)**:
```javascript
// Hemisphere fill
const hemi = new THREE.HemisphereLight(0xb1e1ff, 0xb97316, 0.8);

// Ambient fallback
const ambient = new THREE.AmbientLight(0x404040, 0.3);

// Ceiling lamp
const lamp = new THREE.PointLight(0xffffff, 800.0, 3000.0, 2.0);
lamp.position.set(100, 250, 100);
```

---

## Summary Table: Indoor Lighting Strategy

| System | Ambient Fill | Local Lights | GI | SSAO | Best For |
|--------|--------------|--------------|----|----|----------|
| **Three.js** | AmbientLight + HemisphereLight | PointLight + SpotLight | Community libs | pmndrs/postprocessing | Web, indie |
| **Godot 4** | Environment + SDFGI | OmniLight3D + SpotLight3D | VoxelGI + SDFGI | Built-in | Game dev, rapid prototyping |
| **UzEngine** | SkyLight component | PointLightComponent + SpotLightComponent | None (future) | GTAO postprocess | Professional, custom |

---

## Quick Copy-Paste Templates

### Three.js Indoor Atrium

```javascript
// Sky + fill
const hemi = new THREE.HemisphereLight(0xb1e1ff, 0xb97316, 0.8);
const ambient = new THREE.AmbientLight(0x404040, 0.4);

// Ceiling lamp
const lamp1 = new THREE.PointLight(0xffffff, 1200.0, 4000.0, 2.0);
lamp1.position.set(100, 1200, -50);

// Side lamps
const lamp2 = new THREE.PointLight(0xffffe8, 600.0, 3000.0, 2.0);
lamp2.position.set(-800, 800, -200);

scene.add(hemi, ambient, lamp1, lamp2);
```

### Godot 4 Interior (SDFGI)

```gdscript
var env = Environment.new()
env.sdfgi_enabled = true
env.sdfgi_read_sky_light = false
env.sdfgi_cascades = 4
env.sdfgi_min_cell_size = 50.0
env.sdfgi_y_scale = 0.5

var world_env = WorldEnvironment.new()
world_env.environment = env
add_child(world_env)

# Add 4-5 point lights
for i in range(4):
    var light = OmniLight3D.new()
    light.energy = 1.0
    light.omni_range = 3000.0
    light.position = Vector3(randf_range(-1000, 1000), 500, randf_range(-1000, 1000))
    add_child(light)
```

### UzEngine Interior (Point Lights + SkyLight)

```cpp
// Update SkyLight
auto sky = world.GetSingletonComponent<SkyLightComponent>();
sky.intensity = 2.0f;
sky.indirectLightingIntensity = 2.5f;
sky.lowerHemisphereIsBlack = false;
sky.lowerHemisphereColor = {0.15f, 0.2f, 0.25f};

// Add ceiling light
auto light1 = world.CreateEntity();
world.AddComponent<PointLightComponent>(light1).intensity = 1200.0f;
world.AddComponent<PointLightComponent>(light1).range = 4000.0f;
world.GetComponent<TransformComponent>(light1).SetPosition({-50, 1200, -50});

// Add side lights (2 more)...
```

---

**Reference generated**: 2026-04-06  
**Target audience**: UzEngine developers, graphics programmers  
**See also**: `/docs/analysis/indoor-lighting-analysis.md` for full context
