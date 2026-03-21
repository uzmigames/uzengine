# UE5 Source-First Implementation

**Category**: architecture
**Tags**: ue5-parity, rendering, mandatory, critical

## Description

Before writing ANY engine code, ALWAYS read the corresponding UE5 source at W:\UE_5.6_Source first. Use unreal-architect agent to find the exact file, function, and line number. Implement the SAME math with only variable/function name changes to avoid copyright. Document the UE5 source reference in code comments.

## Example

// ✅ CORRECT — UE5 source referenced, same math\n// Based on UE5 FogRendering.cpp:247-256 (FSceneRenderer::InitFogConstants)\nfloat collapsedPower = std::clamp(\n    -fogInfo.heightFalloff * (observerHeight - fogInfo.height),\n    -126.f + 1.f, 127.f - 1.f);\nfloat collapsedDensity = fogInfo.density * std::pow(2.0f, collapsedPower);\n\n// Based on UE5 HeightFogCommon.ush:192-199 (CalculateLineIntegralShared)\nfloat falloff = max(-127.0f, heightFalloff * rayDirZ);\nfloat lineIntegral = (1.0f - exp2(-falloff)) / falloff;

## When to Use

Every time any rendering, lighting, fog, shadow, physics, or post-processing code is written or modified.

## When NOT to Use

Pure engine infrastructure (memory allocators, file I/O, build system) that has no UE5 equivalent.
