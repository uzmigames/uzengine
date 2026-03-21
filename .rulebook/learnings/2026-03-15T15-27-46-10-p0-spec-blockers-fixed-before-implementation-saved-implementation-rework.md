# 10 P0 spec blockers fixed before implementation — saved implementation rework
**Source**: manual
**Date**: 2026-03-15
**Tags**: specs, validation, p0-blockers, methodology, quality
On 2026-03-03, readiness analysis found 10 P0 spec blockers that would have caused implementation failures. All fixed before coding started: (1) rhi.md missing RhiResourceState/RhiTextureDimension enums, (2) render-graph.md had DX12 direct types instead of RHI handles, (3) physics-system.md had 5 wrong Jolt API signatures, (4) shader-library.md missing ibl.hlsli section, (5) audio-system.md had conflicting CAudioSource definitions, (6-10) various cross-spec inconsistencies. Lesson: always validate specs for API accuracy before implementation — specs written from memory often have wrong function signatures.