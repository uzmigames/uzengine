# Reading file-static CVar pointers for pass gating

**Category**: rendering
**Tags**: cvar, rendering, gating

## Description

GTAOPass::IsEnabledCVar() returned `cv_GTAOEnabled->value` (file-static UZ_CVAR pointer). Scene .uzscene overrides write through CVarRegistry::Set on the registry instance; the cached static pointer can be a different/stale object depending on module/static-init order, so the pass silently never schedules while --diag/logs show the CVar as 1. Symptom set: pass Init logs appear, Execute first-frame logs never appear, no error anywhere. Fix: gate scheduling via CVarRegistry::Get().GetBool("r.X", false). frame_lighting.cpp already carried a warning comment about this exact trap — when a comment says "read via the registry instead", audit ALL accessors of that CVar, not just the call site the comment sits on.

## When to Use

Any per-frame pass gating or scheduling decision driven by a CVar

## When NOT to Use

Hot inner loops where the registry lookup cost matters and the CVar object was provably obtained from the registry post-init
