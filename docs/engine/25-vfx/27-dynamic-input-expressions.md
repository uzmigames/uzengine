# VFX dynamic-input expressions

Compact infix DSL for node-graph input pin bindings. Authors plug small
expressions (multiply, lerp, clamp) into a pin without creating a full
module asset, eliminating boilerplate on common wire transforms.

Reference: UE5 `UNiagaraDynamicInputScript` + `FNiagaraInputScriptCompiler`.

## Grammar (PEG)

```
expr     <- ternary
ternary  <- logic ('?' expr ':' expr)?
logic    <- compare (('&&' | '||') compare)*
compare  <- addSub (('==' | '!=' | '<' | '>' | '<=' | '>=') addSub)?
addSub   <- mulDiv (('+' | '-') mulDiv)*
mulDiv   <- unary (('*' | '/') unary)*
unary    <- ('-' | '!') unary | call
call     <- IDENT '(' argList ')' | primary
argList  <- expr (',' expr)*
primary  <- NUMBER | IDENT | '(' expr ')'
```

## Built-in functions (10)

| Name      | Args | HLSL output                    |
|-----------|------|--------------------------------|
| `add`     | 2    | `(a + b)`                       |
| `sub`     | 2    | `(a - b)`                       |
| `mul`     | 2    | `(a * b)`                       |
| `div`     | 2    | `(a / b)`                       |
| `lerp`    | 3    | `lerp(a, b, t)`                 |
| `clamp`   | 3    | `clamp(x, lo, hi)`              |
| `sin`     | 1    | `sin(x)`                        |
| `cos`     | 1    | `cos(x)`                        |
| `dot`     | 2    | `dot(a, b)`                     |
| `normalize`| 1   | `normalize(v)`                  |

`add`/`sub`/`mul`/`div` are lowered to infix operators so the generated
HLSL reads naturally. All other built-ins emit a direct function call.

## JSON binding

```json
{
  "pin": "n1:color",
  "expr": "lerp(colorStart, colorEnd, normalizedAge)"
}
```

Stored in `VfxNodeGraph::pinExpressions`. At graph compile time the parser
produces an AST and `VfxExprToHLSL` emits the HLSL snippet inline into the
composed shader. Pins with an expression binding take priority over wires.

## API

```cpp
// Parse
auto result = VfxParseExpression("lerp(a, b, t)");
if (result.root) {
    std::string hlsl = VfxExprToHLSL(result.root.get());
    // hlsl == "lerp(a, b, t)"
}

// Built-in query
bool ok = VfxExprIsBuiltinFunction("lerp");  // true
int  n  = VfxExprBuiltinArgCount("lerp");    // 3
```

## Error handling

Parse failures return `root == nullptr` with a human-readable
`errorMessage` and byte `errorOffset` into the source string. The editor
surfaces these inline on the pin; CI validation (8.9.0) can check
expression syntax without a GPU.

## Tests

`runtime/tests/engine/vfx/test_vfx_expression.cpp` covers:

- **Parser**: literal, identifier, add/sub, precedence mul>add, parentheses,
  unary minus/bang, single-arg call, multi-arg call, nested calls, ternary,
  comparison, logic AND/OR, empty input error, mismatched paren error,
  trailing token error.
- **Codegen**: literal→float, ident passthrough, binary parenthesised,
  add/mul/sub/div builtins inline to operators, lerp/clamp/normalize emit
  direct calls, ternary emits conditional, complex nested expression,
  null root→empty string.
- **Built-in registry**: all 10 recognised, arg counts correct, unknown→-1.
