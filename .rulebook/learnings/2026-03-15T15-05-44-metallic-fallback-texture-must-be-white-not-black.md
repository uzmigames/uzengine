# Metallic fallback texture must be WHITE not BLACK
**Source**: manual
**Date**: 2026-03-15
**Tags**: rendering, pbr, materials, textures, fallback, bug-fix
On 2026-03-06, all untextured materials appeared non-metallic regardless of material parameters. Root cause: the metallic fallback texture was BLACK (0,0,0,0) which forced metallic=0. Fixed to WHITE (1,1,1,1) so the material parameter controls metallic value. Rule: all fallback textures should be WHITE except normal maps (flat blue 0.5,0.5,1.0) and displacement maps (black = no offset). This is a common PBR engine pitfall.