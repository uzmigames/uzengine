# Material system has 10+ textures per material — root sig slots are the bottleneck
**Source**: manual
**Date**: 2026-03-15
**Tags**: rendering, materials, root-signature, bindless, blocker
The material system supports: albedo, normal, metallic, roughness, AO, displacement textures + emission + per-material params (envMultiplier, normalStrength, specularMultiplier, selfShadowStrength, cavityStrength). Material CB is 20 DWORDs (b1 slot). With 10 texture SRV slots (t0-t9), each as a descriptor table, the graphics root signature is FULL. Adding IBL textures (env cubemap, irradiance, prefiltered, BRDF LUT) or shadow atlas requires bindless migration. This is the P1 blocker documented in task 3.41.0-architecture-risk-fixes.