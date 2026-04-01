# Phase 4.9.0 — VFX Orbit / Rotate Around Point

Particles orbit a configurable world-space center point on a circular path. Unlike the vortex
module (which applies a tangential force and lets the physics integrator produce a spiral),
orbit DIRECTLY SETS the position offset each frame — the angular position is derived from
`particle.age * orbitSpeed + particleIndex * goldenAngle` (Fibonacci spread). This guarantees
a perfect circle at any speed.

The orbit is additive to the particle's base position (which can still drift or be set by
initial velocity). The orbit axis defines the plane normal; `right` and `up` vectors in that
plane are derived via Gram-Schmidt.

---

## 1. Constant Buffer — VFXUpdateCB

- [ ] 1.1 Add the following block to `VFXUpdateCB` in `runtime/engine/vfx/vfx_viewport_vfx.h`:
  ```cpp
  uint  orbitEnabled;     // 0=off 1=on; default 0
  float orbitRadius;      // orbit circle radius in cm; default 100.0f
  float orbitSpeed;       // radians per second; default 1.0f
  float _orbitPad;        // padding
  float orbitAxisX;       // orbit plane normal X; default 0.0f
  float orbitAxisY;       // orbit plane normal Y; default 0.0f
  float orbitAxisZ;       // orbit plane normal Z; default 1.0f (Z-up plane)
  float _orbitAxisPad;    // padding
  ```
- [ ] 1.2 Verify `VFXUpdateCB` total size is a multiple of 16. Adjust padding if needed.
- [ ] 1.3 Add matching fields to `VFXEmitterConfig` with the same defaults.

## 2. CPU — CB Upload

- [ ] 2.1 In the `VFXUpdateCB` fill function, map all new fields:
  ```cpp
  cb.orbitEnabled  = config.orbitEnabled;
  cb.orbitRadius   = config.orbitRadius;
  cb.orbitSpeed    = config.orbitSpeed;
  cb.orbitAxisX    = config.orbitAxisX;
  cb.orbitAxisY    = config.orbitAxisY;
  cb.orbitAxisZ    = config.orbitAxisZ;
  ```
- [ ] 2.2 Confirm the fill runs before the update `Dispatch()`, not the billboard draw call.

## 3. Shader — particle_update_cs.hlsl

- [ ] 3.1 Add all new `VFXUpdateCB` fields to the cbuffer declaration in `runtime/shaders/vfx/particle_update_cs.hlsl`.
- [ ] 3.2 Implement orbit position override after velocity integration but BEFORE writing the particle position to the buffer:
  ```hlsl
  /// @ref Christensen 2018 "Fibonacci Sphere Sampling" — golden angle for uniform spread
  /// @constants goldenAngle=2.39996323 rad (pi*(3-sqrt(5)))
  if (orbitEnabled)
  {
      float goldenAngle = 2.39996323f;
      float angle = particle.age * orbitSpeed + (float)particleIndex * goldenAngle;

      // Gram-Schmidt: build right/up vectors in the orbit plane
      float3 axis = normalize(float3(orbitAxisX, orbitAxisY, orbitAxisZ));
      float3 arbitrary = abs(axis.z) < 0.9f ? float3(0,0,1) : float3(1,0,0);
      float3 right = normalize(cross(axis, arbitrary));
      float3 up    = cross(axis, right);

      float3 orbitOffset = (cos(angle) * right + sin(angle) * up) * orbitRadius;
      particle.position += orbitOffset; // additive to current position
  }
  ```
- [ ] 3.3 Confirm `orbitEnabled = 0` produces no change to particle position (no-op branch).
- [ ] 3.4 Confirm orbit works correctly when combined with gravity: particles should orbit AND fall simultaneously (orbit is position additive, gravity affects velocity as before).

## 4. Emitter Preset

- [ ] 4.1 Add an "Orbital" preset:
  - `orbitEnabled = 1`
  - `orbitRadius = 150.0f` (cm)
  - `orbitSpeed = 1.2f` (rad/s — roughly one revolution every 5 seconds)
  - `orbitAxisZ = 1.0f` (horizontal orbit plane)
  - `initialVelocity = (0,0,0)` (no radial drift)
  - `lifetime = 8.0f`
  - `particleCount = 64`
- [ ] 4.2 Confirm preset appears in the demo dropdown.

## 5. UI — Physics Category

- [ ] 5.1 In the Physics category of the VFX inspector, add an "Orbit" section:
  ```cpp
  ImGui::SeparatorText("Orbit");
  ImGui::Checkbox("Enable Orbit", (bool*)&config.orbitEnabled);
  if (config.orbitEnabled)
  {
      ImGui::SliderFloat("Orbit Radius",       &config.orbitRadius, 0.0f, 1000.0f, "%.1f cm");
      ImGui::SliderFloat("Orbit Speed (rad/s)", &config.orbitSpeed, -10.0f, 10.0f, "%.2f");
      ImGui::InputFloat3("Orbit Axis",         &config.orbitAxisX);
  }
  ```
- [ ] 5.2 Add tooltip on Orbit Axis: `"World-space normal of the orbit plane. (0,0,1) = horizontal circle, (0,1,0) = vertical circle facing X."`.

## 6. Visual Verification

- [ ] 6.1 Build runtime then demo.
- [ ] 6.2 Run with "Orbital" preset, capture at frame 60 (several full revolutions):
  ```
  vfx_demo.exe --screenshot-stdout --screenshot-frame 60 --frames 65
  ```
- [ ] 6.3 Visually verify: particles form a visible ring around the emitter origin. No particles clumping at a single point.
- [ ] 6.4 Change axis to `(0,1,0)` and capture — ring should now be vertical.
- [ ] 6.5 `orbitEnabled = 0` — particles must behave identically to a standard emitter with no orbit influence.

## 7. Documentation

- [ ] 7.1 Add `/// @ref Christensen 2018 "Fibonacci Sphere Sampling"` citation above the golden angle constant in the shader.
- [ ] 7.2 Update VFX guide with an "Orbit" section explaining the difference between orbit (position override) and vortex (force-based), and when each is appropriate.
