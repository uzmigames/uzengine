# MaterialGraph test lifetime correction - 2026-10-14

## Root cause

The reported ID-zero collision and codegen state leak were two observations of
undefined behavior in the tests. `MaterialGraph` stores expressions in a
`std::vector<MaterialExpression>` (`runtime/engine/material/material_graph.h:195`).
`AddExpression` calls `push_back` and returns `back()`
(`runtime/engine/material/material_graph.cpp:73-77`). A subsequent addition can
reallocate storage and invalidate all previously returned references.

`MaterialGraphSerialization.RoundTrip_NodeCount_Connections_ParamValues` kept
the scalar reference while adding Multiply, then read the invalid reference's
`id` for connections, lookups, and assertions. The apparent zero was not an
allocated expression ID: PBROutput already uses 1, and `nextExprId_` starts at 2
(`runtime/engine/material/material_graph.h:196`). Zero remains the disconnected
sentinel in `MatPin::IsConnected`; changing that sentinel would hide the invalid
read rather than repair it.

`MaterialGraphHlslCodegen.Add_ExplicitVariables_ConnectedInputs` and
`Multiply_ExplicitVariables_ConnectedScalarInputs` similarly retained scalar
references while adding more nodes. Their results depended on the contents of
freed vector storage, which vary with allocator history and therefore preceding
tests. The shared influence was the allocator, not persistent compiler state.
`HlslMaterialTranslator::Compile` resets its instance-owned buffers, memoization,
graph pointer, sort order, binding counters, and result on every call
(`runtime/engine/material/compiler/hlsl_material_translator.cpp:620-627`).

## Fix

Capture each expression ID while its returned reference is valid, before adding
another expression. Use the captured ID for later connections and assertions.
The serialization capture is at
`runtime/tests/material/test_material_graph.cpp:111`; the Add and Multiply
captures are at lines 565 and 612. Apply the same correction to the neighboring
multi-node topology, dead-code, checker, cache-hash, and parameter-binding tests
in this file, preserving their existing assertions.

The API comment at `runtime/engine/material/material_graph.h:120` now states the
reference-invalidation contract and directs callers to retain IDs and reacquire
nodes with `FindExpression`. No container, ID-allocation, sentinel, compiler,
test-execution ordering, or VFX behavior was changed.

## Regression coverage

- `MaterialGraphSerialization.RoundTrip_ConnectionsSurviveStorageGrowth`
  (`runtime/tests/material/test_material_graph.cpp:26`) adds nodes until capacity
  grows, checks nonzero IDs, and verifies serialized wiring and scalar values.
  It forces the storage-growth condition without dereferencing stale references.
- `MaterialGraphHlslCodegen.AlternatingGraphs_ReuseTranslatorWithoutStateLeak`
  (`runtime/tests/material/test_material_graph.cpp:55`) compiles Add, Multiply,
  then Add with one translator and overlapping graph-local IDs. Each result must
  contain the correct connected-input operator and equal a fresh translator's
  output.

Before the correction, the broad `MaterialGraph*` filter ran 39 tests:
37 passed and the two explicit connected-input codegen tests failed.
Evidence: `.sandbox/stabilization/material-graph-before-20261014.json`.
Final full-suite results and remaining suite ownership are recorded in
[tasks.md](tasks.md).
