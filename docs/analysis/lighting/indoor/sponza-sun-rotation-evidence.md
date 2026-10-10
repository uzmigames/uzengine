# Sponza solar rotation and sky opening evidence

> **Origin**: `test/windows-sponza-parity @ 25cd0c5f` (`E:\SponzaTest`), ported verbatim on 2026-09-08 by `phase0_sponza-parity-06-qa-harness-validation`.
> Paths such as `scripts/*_artifacts/`, `scripts/sponza-*.png|log|json|sha256.txt` refer to evidence that is **not in Git**; the executable identities quoted are SponzaTest builds, not this repository's.


Inspected 2026-09-06. Native CPU tests use production math sources; no GPU was used for this audit.

## Confirmed rotation defect

`sample/content/scenes/sponza.uzscene` authors Sun Euler degrees `[-56,79,0]`. `scene_loader.cpp:206` passes these to `Quat::FromEuler`; `light_renderer.cpp:51` rotates the +X forward axis.

`quaternion.cpp:33` previously called `XMQuaternionMultiply(qYaw, XMQuaternionMultiply(qPitch, qRoll))`. DirectXMath returns Q2*Q1, applying Q1 followed by Q2, as documented in its bundled `DirectXMathMisc.inl:88` and [Microsoft reference](https://learn.microsoft.com/en-us/windows/win32/api/directxmath/nf-directxmath-xmquaternionmultiply). The implementation therefore applied yaw before pitch, contrary to its documented order.

- Before: direction `(0.106699, 0.981627, 0.158188)`, matching live PerFrameCB diagnostics. Solar elevation approximately 9.1 degrees.
- Correct documented composition: `(0.106699, 0.548919, 0.829038)`, solar elevation 56 degrees.
- Minimal fix: `XMQuaternionMultiply(XMQuaternionMultiply(qRoll, qPitch), qYaw)`. General quaternion operator multiplication is unchanged.

`powershell -NoProfile -ExecutionPolicy Bypass -File scripts/test_quaternion.ps1` builds the actual quaternion.cpp, mat4.cpp and vector3.cpp with the two existing quaternion GoogleTest suites. Added regressions exercise the authored Sponza direction, zenith with 90-degree yaw, and three combined-axis Euler round trips.

Baseline: 35/38 passed; all three new regressions failed. After correction: 38/38 passed. Logs: `scripts/test_quaternion_before.log` and `scripts/test_quaternion_after.log`.

Scope: all callers of FromEuler with combined nonzero axes now receive the documented roll→pitch→yaw order. Other scenes or Lua callers that compensated for the old ordering can change orientation. Pure-axis rotations retain their behavior. This is not a scene-specific exception.

## Exact geometry rays with the corrected authored sun direction

Method: Node.js built-in fs reads every referenced `.uzmesh` in the Sponza prefab; float32 positions and uint32 LOD0 indices are tested with double-precision, two-sided Möller–Trumbore ray/triangle intersections. Prefab transforms are identity. Seven instances containing 132 triangles with authored Translucent material graphs are excluded, matching corrected GDF collection. Remaining geometry: 3,747,154 triangles. Masked geometry is treated as solid; alpha coverage was not evaluated.

| Origin, cm | Vertical +Z ray | Solar ray, corrected direction |
|---|---|---|
| Camera A `(86,881,159)` | ceiling_plaster_02, 324.409 cm | ceiling_plaster_02, 364.508 cm |
| Camera C `(-211,-270,135)` | Sky | Sky |
| Camera E `(-236,224,74)` | Sky | dirt_decal, 1071.209 cm |
| Center `(0,0,150)` | Sky | ornament_01, 1428.581 cm |
| Center `(0,0,600)` | Sky | Sky |
| Center `(0,0,1200)` | Sky | Sky |

These are exact geometry checks, not GDF readbacks. They establish that the atrium has vertical sky openings and that Camera C has an unobstructed corrected solar ray. They do not establish what the prior incorrect low-elevation solar direction could see. Coarse GDF filtering and shell expansion can still alter visibility.

## Independent sky energy mismatch identified

The procedural cubemap integrates scattering radiance and then divides its output by pi (`sky_cubemap_cs.hlsl:141,161`). The visible sky shader returns integrated radiance directly (`sky_atmosphere_ps.hlsl:192,232`). CPU capture also replaces RGB sun illuminance with its vector length (`atmosphere_renderer.cpp:215`). These conventions are inconsistent independently of the rotation defect.

[Hillaire's primary reference implementation](https://github.com/sebh/UnrealEngineSkyAtmosphere/blob/master/Resources/RenderSkyRayMarching.hlsl) integrates RGB solar illuminance into scattered luminance and returns L unchanged; the Lambertian ground reflection carries its own 1/pi. Local irradiance_cs uses cosine-weighted averaging (already E/pi), the prefilter preserves radiance through normalized weights, and the SH packing includes cosine convolution. Sky producer correction is owned and validated by the parent task.
