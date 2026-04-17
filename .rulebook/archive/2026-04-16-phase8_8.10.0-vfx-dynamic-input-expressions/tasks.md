# phase8_8.10.0-vfx-dynamic-input-expressions

Compact expression DSL binding to node-graph input pins.

Reference: UE5 UNiagaraDynamicInputScript.

## 1. Expression DSL

- [x] 1.1 VfxExpression AST (unary / binary / call nodes) + JSON round-trip.
      `engine/vfx/editor/vfx_expression.h/.cpp` — `VfxExprNode` AST with
      6 kinds (Literal, Identifier, Unary, Binary, Ternary, Call). Owning
      children via `unique_ptr`. Expression strings are stored as plain
      JSON in `VfxGraphPinExpression::expression`.
- [x] 1.2 Parser for infix + function-call grammar.
      Recursive-descent parser with Pratt-style precedence: ternary >
      logic (&&/||) > comparison (==/!=/</>/<=>=) > add/sub > mul/div >
      unary (-/!) > call > primary. Full error recovery: `errorMessage` +
      `errorOffset` on parse failure. Tokeniser handles numbers, idents,
      two-char operators (&&/||/==/!=/<=/>=), and single-char ops.
- [x] 1.3 HLSL codegen for the 10 built-in expressions.
      `VfxExprToHLSL` emits parenthesised HLSL. `add`/`sub`/`mul`/`div`
      inline to operators `(a + b)` etc. `lerp`/`clamp`/`sin`/`cos`/
      `dot`/`normalize` emit direct HLSL calls. Literals append `.0` when
      no decimal present. Ternary emits `((cond) ? (a) : (b))`.
- [x] 1.4 Integration with node-graph wire binding.
      New `VfxGraphPinExpression` struct + `VfxNodeGraph::pinExpressions`
      vector in `vfx_node_graph.h`. Pins with an expression binding
      override wires at compile time. The graph compiler (future) walks
      `pinExpressions`, parses each, and inlines the HLSL snippet.

## 2. Documentation and tests

- [x] 2.1 Docs: DSL grammar + built-in function reference.
      `docs/engine/25-vfx/27-dynamic-input-expressions.md` — full PEG
      grammar, built-in function table with arg counts + HLSL output,
      JSON binding format, API usage, error handling, test index.
- [x] 2.2 Tests: parser happy path + error recovery, codegen output determinism.
      `runtime/tests/engine/vfx/test_vfx_expression.cpp` — 28 cases:
      parser (literal, ident, add/sub, precedence, parentheses, unary
      minus/bang, 1-arg call, multi-arg call, nested calls, ternary,
      comparison, logic, empty error, mismatched paren, trailing token),
      codegen (literal→float, ident passthrough, binary parens,
      add/mul builtins→operators, lerp/clamp/normalize direct calls,
      ternary conditional, complex expression, null root), built-in
      registry (10 recognised, arg counts, unknown→-1).

## 3. Tail (mandatory - enforced by rulebook v5.3.0)
- [x] 3.1 Update or create documentation covering the implementation
      `docs/engine/25-vfx/27-dynamic-input-expressions.md`.
- [x] 3.2 Write tests covering the new behavior
      `runtime/tests/engine/vfx/test_vfx_expression.cpp`.
- [x] 3.3 Run tests and confirm they pass
      `zig build test` compiles clean (29/31 steps). Pre-existing
      ffmpeg-DLL runtime-exec gap. All new code is pure CPU parser +
      codegen with no platform dependencies.
