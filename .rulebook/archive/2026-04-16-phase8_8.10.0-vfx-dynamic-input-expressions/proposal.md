# Proposal: phase8_8.10.0-vfx-dynamic-input-expressions

## Why
Niagara's Dynamic Input Scripts let authors plug small expressions into a module's input pin (multiply, lerp, clamp) without creating a full module asset. Saves boilerplate on common wire transforms.

## What Changes
Add a compact expression DSL evaluated at graph-compile time: input pins can bind to an expression tree instead of a concrete value. Ship 10 built-in expressions (add / sub / mul / div / lerp / clamp / sin / cos / dot / normalize).

## Impact
- Affected specs: docs/engine/25-vfx/
- Affected code: runtime/engine/vfx/editor/vfx_node_graph.h + new vfx_expression.h/.cpp
- Breaking change: NO
- User benefit: Inline expressions eliminate boilerplate module authoring.
- Estimated effort: M

## Source
UE5 UNiagaraDynamicInputScript + FNiagaraInputScriptCompiler.
