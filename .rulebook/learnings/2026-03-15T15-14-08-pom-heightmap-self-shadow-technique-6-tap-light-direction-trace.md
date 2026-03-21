# POM heightmap self-shadow technique: 6-tap light-direction trace
**Source**: manual
**Date**: 2026-03-15
**Tags**: rendering, pom, parallax, self-shadow, displacement, materials
Parallax occlusion mapping includes a self-shadow feature: a 6-tap trace along the light direction on the displacement map produces contact shadows in ripple valleys, mortar lines, and fabric weave. Per-material controls: selfShadowStrength and cavityStrength. The POM system uses steep parallax with 8-32 adaptive layers based on view angle, plus linear interpolation for smooth results. Quality improvements include cavity darkening, AO, seam fade at UV boundaries, and grazing angle blend to avoid artifacts.