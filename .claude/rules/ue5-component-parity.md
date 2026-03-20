# Rendering Component Behavioral Parity — ZERO Simplification

## Rule (applies to ALL agents — NO EXCEPTIONS)

Every UzEngine component, system, and feature that has a reference equivalent MUST replicate the **exact same behavior, parameters, defaults, ranges, and logic** as the reference. This is not limited to numeric constants — it covers the ENTIRE component interface.

## What This Means

1. **Same parameters**: If the reference implementation's `UExponentialHeightFogComponent` has 47 properties, UzEngine's `HeightFogComponent` has 47 properties. Not 12. Not "the most important ones". ALL of them.

2. **Same defaults**: If reference defaults `FogDensity` to `0.02f`, ours is `0.02f`. Not `0.05f` because "it looks better". Read the reference constructor or `GetDefaultObject()`.

3. **Same behavior**: If the reference computes inscattering color from the sky atmosphere at runtime, we compute inscattering color from the sky atmosphere at runtime. We do NOT hardcode `{0.45, 0.55, 0.70}` because "it approximates a blue sky".

4. **Same ranges/validation**: If the reference clamps a property to `[0, 1]` via `UPROPERTY(meta=(ClampMin=0, ClampMax=1))`, we clamp it the same way.

5. **Same update logic**: If the reference recalculates a derived value when a property changes, we recalculate the same derived value.

## ABSOLUTELY FORBIDDEN

- **Simplifying a component** by removing parameters the reference has ("we don't need this one")
- **Inventing defaults** without reading reference engine source (`GetDefaultObject()`, constructor, `.cpp`)
- **Replacing runtime computation with hardcoded values** (e.g., auto sky color → hardcoded RGB)
- **Omitting update/recalculation logic** that the reference performs when properties change
- **Combining or renaming parameters** in ways that change semantics
- **Adding parameters the reference doesn't have** (no "artistic controls", no "UzEngine enhancements")
- **Guessing** at what a parameter does based on its name — READ THE CODE

## How to Implement a New Component

1. **Research the published reference** for the rendering technique (SIGGRAPH papers, GDC talks, etc.)
2. **Identify ALL parameters** the technique requires
3. **Research standard default values** from published references
4. **Implement ALL properties** with the correct defaults
5. **Implement ALL behavior** — same computation, same update triggers
6. **Cite the reference** — every property must have a `@ref Author Year "Title"` comment

## Intellectual Property Protection

To protect against IP infringement:
- **NEVER** copy code verbatim from any proprietary codebase
- **NEVER** use proprietary function names, struct names, or identifiers
- **NEVER** cite proprietary source files or line numbers in code
- **DO** use UzEngine-specific naming for all functions and types
- **DO** cite published papers, standards, and techniques as references
- **DO** keep the same mathematical formulas (math is not copyrightable)

## Example

```cpp
// WRONG — simplified, invented defaults, missing parameters
struct HeightFogComponent {
    float density = 0.05f;        // invented
    float3 color = {0.5, 0.6, 0.8}; // invented
    float height = 0.0f;
    // missing parameters the technique requires
};

// CORRECT — complete implementation with published references
/// @ref Hillaire 2016 "Physically Based & Unified Volumetric Rendering"
struct HeightFogComponent {
    /// @ref standard exponential fog density parameter, default 0.02
    float fogDensity = 0.02f;
    /// @ref exponential height falloff, default 0.2
    float fogHeightFalloff = 0.2f;
    // ... ALL properties, ALL cited from published references
};
```

## Why

Industry-standard rendering techniques have been validated by developers with decades of experience across thousands of shipped titles. Claude does not have the expertise to decide which parameters "matter" or what "good defaults" are. Every time simplification was attempted, the result was visibly wrong.
