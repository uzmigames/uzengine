# Render pass pipeline: 10 passes available, render graph schedules automatically
**Source**: manual
**Date**: 2026-03-15
**Tags**: rendering, render-graph, passes, pipeline, inventory
Current render pass inventory (all registered via render graph): Shadow (CSM 2x2 atlas, 4096x4096 D32F), GBuffer (deferred geometry → 3 RTs + depth), Lighting (fullscreen deferred), PBR (forward single-pass), Sky Atmosphere (Hillaire 2020), Transmittance LUT (compute), Height Fog, GBuffer Debug, Debug View (13 F-key modes), Velocity (motion vectors RG16F). All passes accept RgBuilder& and *PassDesc struct. Render graph compiler does topological sort + barrier scheduling automatically. Pass culling removes unused outputs.