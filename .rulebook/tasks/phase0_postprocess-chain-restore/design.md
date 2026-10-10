# Design: documenting the scaffold chain

No rendering change. The restoration agent compared 7eea6b95^ and the SponzaTest reference, read the scaffold
shaders, smoke-ran defaults / TAA off / lens flare / DOF before and after its build (identical pass lists, 0 errors),
and checked the Lua setter with a temporary script (before: enabled=true, cvar=0 right after the call, false / 0 five
and twenty frames later; after: true / 1 at all three points). Other losses of bd4db199's return, recorded for later
tasks: the previous Hi-Z fields were written only in the dead section, so two-phase occlusion culling never engages
(only with r.GpuCull=1, off by default); the PassInspector targets bloom.result, motionblur.result, dof.result,
dof.coc, final.ldr and postprocess.color_lut are not registered; ctx.motionBlurActive / dofActive / fxaaActive /
casActive and the *SrvForTonemap chain are computed every frame and read by nothing; SetTaaEnabled has the same
setter defect (no callers).
