# UE5 Component Behavioral Parity — ZERO Simplification

## Rule (applies to ALL agents — NO EXCEPTIONS)

Every UzEngine component, system, and feature that has a UE5 equivalent MUST replicate the **exact same behavior, parameters, defaults, ranges, and logic** as UE5. This is not limited to numeric constants — it covers the ENTIRE component interface.

## What This Means

1. **Same parameters**: If UE5's `UExponentialHeightFogComponent` has 47 properties, UzEngine's `HeightFogComponent` has 47 properties. Not 12. Not "the most important ones". ALL of them.

2. **Same defaults**: If UE5 defaults `FogDensity` to `0.02f`, ours is `0.02f`. Not `0.05f` because "it looks better". Read the UE5 constructor or `GetDefaultObject()`.

3. **Same behavior**: If UE5 computes inscattering color from the sky atmosphere at runtime, we compute inscattering color from the sky atmosphere at runtime. We do NOT hardcode `{0.45, 0.55, 0.70}` because "it approximates a blue sky".

4. **Same ranges/validation**: If UE5 clamps a property to `[0, 1]` via `UPROPERTY(meta=(ClampMin=0, ClampMax=1))`, we clamp it the same way.

5. **Same update logic**: If UE5 recalculates a derived value when a property changes, we recalculate the same derived value.

## ABSOLUTELY FORBIDDEN

- **Simplifying a component** by removing parameters UE5 has ("we don't need this one")
- **Inventing defaults** without reading UE5 source (`GetDefaultObject()`, constructor, `.cpp`)
- **Replacing runtime computation with hardcoded values** (e.g., auto sky color → hardcoded RGB)
- **Omitting update/recalculation logic** that UE5 performs when properties change
- **Combining or renaming parameters** in ways that change semantics
- **Adding parameters UE5 doesn't have** (no "artistic controls", no "UzEngine enhancements")
- **Guessing** at what a parameter does based on its name — READ THE CODE

## How to Implement a New Component

1. **Find the UE5 equivalent** at `W:\UE_5.6_Source`
2. **Read the `.h` file** — list EVERY `UPROPERTY` (name, type, default, category, meta)
3. **Read the constructor** — note every default value assignment
4. **Read the `.cpp`** — understand the update/tick/recalculation logic
5. **Implement ALL properties** with the EXACT same defaults
6. **Implement ALL behavior** — same computation, same update triggers
7. **Cite the source** — every property must have a `@source UE5 <file>:<line>` comment

## Intellectual Property Protection

To respect Epic's IP while maintaining behavioral parity:
- **DO NOT** copy UE5 code verbatim — rewrite in your own style
- **DO** use different variable names, different code structure
- **DO** keep the same mathematical formulas (math is not copyrightable)
- **DO** keep the same parameter names where they form a public API convention
- **DO** cite the UE5 source file and line as a reference (like citing a paper)

## Example

```cpp
// WRONG — simplified, invented defaults, missing parameters
struct HeightFogComponent {
    float density = 0.05f;        // invented
    float3 color = {0.5, 0.6, 0.8}; // invented
    float height = 0.0f;
    // missing 40+ UE5 parameters
};

// CORRECT — full UE5 parity, cited source
/// @source UE5 ExponentialHeightFogComponent.h:28-180
struct HeightFogComponent {
    /// @source UE5 ExponentialHeightFogComponent.h:42 — FogDensity default 0.02
    float fogDensity = 0.02f;
    /// @source UE5 ExponentialHeightFogComponent.h:48 — FogHeightFalloff default 0.2
    float fogHeightFalloff = 0.2f;
    // ... ALL 47 properties, ALL cited, ALL with UE5-exact defaults
};
```

## Why

The user's explicit directive: UE5 has been validated by developers with decades of experience across thousands of shipped titles. Claude does not have the expertise to decide which parameters "matter" or what "good defaults" are. Every time simplification was attempted, the result was visibly wrong. The UE5 source exists at `W:\UE_5.6_Source` specifically so every value can be verified.
