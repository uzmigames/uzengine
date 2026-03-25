# Proposal: phase3a-3.67.0-vfx-rendering-quality

## Why
With VFX passes connected to the pipeline (3.66.0), the next priority is rendering quality. GPU particle sort eliminates incorrect alpha blending order, flipbook animation brings sprites to life, distortion/refraction adds heat shimmer and glass effects, and TAA motion vectors prevent particle ghosting. These are the quality features that make VFX visually credible.

## What Changes
- GPU radix sort: particles sorted by camera depth before DrawIndexedIndirect for correct alpha blending
- Flipbook/SubUV animation: ComputeFlipbookFrame() utility with linear/random modes, two-frame blending in billboard PS
- Distortion accumulation pass: RGBA16F UV-offset target + resolve pass for heat haze, energy shields, glass refraction
- TAA motion vectors: per-particle screen-space velocity written from billboard VS, integrated into existing TAA pass
- New demos: flipbook fire, heat shimmer, sorted fountain

## Impact
- Affected specs: vfx-gpu-particle-core, vfx-modules-spawn-update-render
- Affected code: runtime/engine/vfx/core/, runtime/engine/vfx/passes/, runtime/shaders/vfx/
- Breaking change: NO
- User benefit: Particles no longer show alpha sorting artifacts; animated sprites play correctly; heat haze and glass distortion effects are possible; fast particles integrate cleanly with TAA
