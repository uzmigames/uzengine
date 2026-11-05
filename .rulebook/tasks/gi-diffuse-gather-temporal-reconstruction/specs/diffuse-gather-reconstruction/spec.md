# Spec — diffuse final-gather temporal reconstruction

The diffuse final-gather SHALL reconstruct a temporally STABLE result on static, accepted receivers while
preserving response to disocclusion and scene motion.

- The gather temporal accumulation MUST NOT impose a fixed minimum replacement weight on a converged,
  100%-accepted, low-variance static receiver; the effective history length SHALL grow under a confidence gate.
- With the feature CVar OFF, the rendered output MUST be byte-identical to the pre-change render.
- The fix MUST NOT darken the mean indirect contribution or over-blur surface detail; per-component means and
  detail SHALL be retained within tolerance.
- The fix MUST preserve disocclusion and camera/light-motion response (no new ghosting/lag).

#### Scenario: static curved diffuse surface converges to a stable image
Given material_showcase with all GI/Indirect enabled and a fixed camera
When 60 consecutive frames are captured at frames 90-149 and again at 600-659 with the feature ON
Then the Sphere_Diffuse and Sphere_GreenPlastic body linear-HDR temporal std move toward the Glass/Chrome
     control range, with per-component means preserved.

#### Scenario: OFF is byte-identical
Given the feature CVar set OFF
When the reflection-reference frame-95 is captured against a fresh pre-change baseline
Then the max code delta is 0.

#### Scenario: motion response preserved
Given a moving camera and a changing light
When the ON path renders the sequence
Then no new ghosting or temporal lag appears versus the current render.
