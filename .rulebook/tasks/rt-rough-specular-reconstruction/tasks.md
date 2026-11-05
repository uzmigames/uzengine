# Rough-specular reflection temporal reconstruction (2026-10-29)

Kills the "crawling" reflection on rough metal (material_showcase DarkMetal), root-caused to one GGX ray/texel
(frame%64) + a curvature-starved SSR denoise. Diagnosis:
[[reference/rendering/sphere-crawl-material-showcase-2026-10-29]] + `_evidence/gi/sphere-crawl/`.

- [x] Reproduce the DarkMetal fixed-camera baseline (frames 90-149 + 600-659) and record body + specular-
      indirect temporal std as the target; capture the glossy-vs-mirror + no_stochastic_specular references.
- [x] Export rough-reflection guides (roughness, hit-distance, normal) for the specular reconstruction.
- [x] Replace the flat normalCosine^512 spatial stop (ssr_spatial_denoise_cs.hlsl) with a roughness/hit-
      distance-scaled support: enough effective samples on curved rough normals, still tight for the mirror.
- [x] Increase effective samples via temporal/spatial reuse of the GGX ray (keep VNDF-throughput energy
      correct); evaluate the frame%64 IGN advance (ReflectionSample2D/ReflectionIGN) for decorrelation.
- [x] Add a variance-driven temporal clamp before scene-color TAA so the rough specular is reconstructed
      pre-TAA rather than fed as noise.
- [x] Gate behind a CVar (r.RayTracing.Reflections.RoughReconstruct, default OFF) so OFF reproduces the
      current SSR/RT render byte-identical; do NOT regress the mirror (roughness<0.04) path.
- [x] Validate + report: DarkMetal body temporal std toward the Glass/Chrome range at frames 90-149 AND
      600-659; specular ENERGY preserved (mean within a few %, no darkening) + no over-blur of glossy detail;
      OFF byte-identical; motion/disocclusion response preserved; pinned-zig build + test:layouts + gtests green.

Validation: `E:/UzmiEngine/_evidence/gi/rough-specular/report.md`. The gated experiment reduces crawl,
but detail/motion preservation and strict material OFF-repeat identity do not pass cleanly. Keep default OFF.
