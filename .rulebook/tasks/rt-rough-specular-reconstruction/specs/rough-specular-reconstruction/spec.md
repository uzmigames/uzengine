# Spec — rough-specular reflection temporal reconstruction

The rough-specular reflection SHALL reconstruct a temporally STABLE result on curved rough surfaces while
preserving specular energy, glossy detail, the mirror path, and motion response.

- The rough-specular signal MUST be reconstructed (variance-driven, guide-aware) before scene-color TAA, not
  fed to TAA as per-frame changing noise.
- With the feature CVar OFF, the rendered output MUST be byte-identical to the pre-change render.
- The fix MUST preserve mean specular energy (within a few percent) and MUST NOT over-blur glossy detail.
- The mirror path (roughness below the mirror threshold) MUST remain unchanged.

#### Scenario: static rough metal converges to a stable reflection
Given material_showcase with all GI/Indirect enabled and a fixed camera
When 60 consecutive frames are captured at 90-149 and again at 600-659 with the feature ON
Then the Sphere_DarkMetal body linear-HDR temporal std moves toward the Glass/Chrome control range, with
     specular mean energy preserved.

#### Scenario: OFF is byte-identical and the mirror is unchanged
Given the feature CVar OFF (and, separately, a mirror-roughness surface)
When the reflection-reference frame-95 is captured against a fresh pre-change baseline
Then the max code delta is 0 and the mirror reflection is identical.

#### Scenario: energy and detail preserved
Given the ON path on rough metal
When the converged reflection is compared to the current render
Then the specular mean energy is within a few percent and glossy detail is not over-blurred.
