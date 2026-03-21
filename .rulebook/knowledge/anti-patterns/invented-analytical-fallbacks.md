# Invented Analytical Fallbacks

**Category**: architecture
**Tags**: ue5-parity, rendering, lighting, fog, shadows, critical

## Description

NEVER create analytical approximations, estimated coefficients, or invented fallback code paths when the correct implementation exists in UE5 source. Examples of violations: inventing SH irradiance ratios (0.7, 0.8, 1.0) instead of reading cubemap, adding minimum clamps like max(value, 0.15), adding compensation factors like intensity * 1.5, guessing fog parameters instead of reading UE5 InitFogConstants.

## Example

// ❌ WRONG — invented analytical fallback\nVec3 skyCol = {0.7f * skyFactor, 0.8f * skyFactor, 1.0f * skyFactor}; // GUESSED\nsl.intensity = 1.5f; // "compensate for analytical SH"\nskyInscatter = max(skyInscatter, float3(0.15, 0.2, 0.35)); // invented minimum\n\n// ✅ CORRECT — read UE5 source, implement same math\n// Based on UE5 FogRendering.cpp:247-256 InitFogConstants\nfloat collapsedPower = clamp(-heightFalloff * (camZ - fogHeight), -126, 127);\nfloat rayOriginTerms = density * pow(2.0f, collapsedPower);

## When to Use

Always check for this anti-pattern before committing any rendering, lighting, fog, shadow, or physics code.

## When NOT to Use

N/A — this is always an anti-pattern. There are no exceptions.
