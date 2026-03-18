/// UzEngine Pipeline Library — Implementation.
///
/// Split into separate files under pipeline/ by domain:
///   pipeline/pipeline_lifecycle.cpp    — Init, Shutdown, shader helpers, PSO cache, warmup
///   pipeline/pipeline_geometry.cpp     — ForwardPBR, GBuffer, Lighting, Debug, DepthPrepass
///   pipeline/pipeline_shadow.cpp       — Shadow depth, instanced variants
///   pipeline/pipeline_atmosphere.cpp   — Sky, transmittance LUT, multi-scatter, cubemap, fog
///   pipeline/pipeline_postprocess.cpp  — Tonemap, velocity, HiZ, DFS, TAA, SSR, color grading, motion blur, DOF, FXAA
///
/// This file is intentionally empty. The build system compiles pipeline/*.cpp
/// directly via addCppDir("engine/renderer/core/pipeline").
