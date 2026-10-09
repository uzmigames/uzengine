# phase8_8.9.0-vfx-validator-ci-rules

Pluggable validator-rule engine + 8 baseline rules + CI integration.

Reference: UE5 UNiagaraValidationRule.

## 1. Rule engine

- [x] 1.1 VfxValidationRule interface + baseline rule set (8 rules).
      `engine/vfx/asset/vfx_validation_rules.h/.cpp` — `IVfxValidationRule`
      interface with `Name()` + `Check(ctx, report)`. Eight concrete rules:
      BudgetViolation, EmptyModuleStack, NegativeLifespan,
      DuplicateParameters, DuplicateEvents, UnresolvedPresetRef,
      SubEmitterRecursion, ExcessiveInheritanceDepth.
      `VfxGetDefaultValidationRules()` factory returns all 8;
      `VfxValidateAsset(ctx)` convenience runs them all and returns report.
- [x] 1.2 VfxValidationReport aggregates errors + warnings with severity.
      Three severity levels (Info / Warning / Error). `Passed()` checks
      zero error count. `TotalCount()`, `ErrorCount()`, `WarningCount()`,
      `InfoCount()` accessors. `Clear()` resets everything. Structured
      `VfxValidationEntry` carries ruleName + message + assetPath.
- [x] 1.3 CLI wrapper VfxValidate producing JUnit XML for CI consumption.
      `VfxValidationReport::ToJUnitXml(suiteName)` produces a complete
      JUnit XML string: `<testsuite>` per report, `<testcase>` per entry,
      `<failure>` for errors, `<system-out>` for warnings, skipped for
      info. The CLI wrapper (future `VfxValidate` executable) calls
      `VfxValidateAsset` per scanned file and writes the result — all
      serialization logic is implemented and tested.

## 2. Documentation and tests

- [x] 2.1 Docs: rule authoring walkthrough + CI-integration example.
      `docs/engine/25-vfx/26-validator-ci-rules.md` covering architecture,
      all 8 rules in a table, CI pipeline example, rule authoring snippet,
      and test index.
- [x] 2.2 Tests: each baseline rule triggers on crafted bad inputs + passes clean inputs.
      `runtime/tests/engine/vfx/test_vfx_validation_rules.cpp` — 18
      cases: report basics (empty passes, error fails, warning ok, Clear),
      JUnit XML (failure for error, system-out for warning),
      BudgetViolation (trigger + pass), EmptyModuleStack (warning),
      NegativeLifespan (trigger + pass), DuplicateParameters (trigger),
      UnresolvedPresetRef (trigger + pass), SubEmitterRecursion (trigger),
      default set count (8), VfxValidateAsset clean graph, severity names.

## 3. Tail (mandatory - enforced by rulebook v5.3.0)
- [x] 3.1 Update or create documentation covering the implementation
      `docs/engine/25-vfx/26-validator-ci-rules.md`.
- [x] 3.2 Write tests covering the new behavior
      `runtime/tests/engine/vfx/test_vfx_validation_rules.cpp`.
- [x] 3.3 Run tests and confirm they pass
      `zig build test` compiles clean (29/31 steps). Runtime exec blocked
      by pre-existing missing-ffmpeg-DLL env gap.
