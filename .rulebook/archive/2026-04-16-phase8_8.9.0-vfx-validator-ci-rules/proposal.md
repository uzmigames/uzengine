# Proposal: phase8_8.9.0-vfx-validator-ci-rules

## Why
The existing VfxGraphValidator checks structural problems (cycles, duplicate params). Niagara's Validator Rules add authored domain constraints: no sub-emitter with >1000 particles, must define a LOD curve, material binding must resolve. Reports feed both editor warnings and CI.

## What Changes
Extend the validator with a pluggable rule interface: each rule implements Check(asset, outErrors). Ship 8 baseline rules covering budget violations, missing material bindings, unresolved preset references, LOD absence, sub-emitter recursion. CLI: VfxValidate produces JUnit XML.

## Impact
- Affected specs: docs/engine/25-vfx/
- Affected code: runtime/engine/vfx/asset/vfx_graph_validator.h + new vfx_validation_rules.h
- Breaking change: NO
- User benefit: CI catches broken VFX assets before they land in main.
- Estimated effort: M

## Source
UE5 UNiagaraValidationRule + FNiagaraValidationContext.
