# VFX validator CI rules

Pluggable domain-constraint engine for VFX assets. Extends the structural
validator (vfx_graph_validator.h) with authored rules that run in both the
editor (inline warnings) and CI (JUnit XML gate).

Reference: UE5 `UNiagaraValidationRule` + `FNiagaraValidationContext`.

## Architecture

```
IVfxValidationRule::Check(ctx, report)
         ↓
VfxValidationContext  →  VfxValidationReport
         ↓                      ↓
  (graph / system /      JUnit XML string
   emitter pointers)     for CI consumption
```

- **Context** — read-only pointers to the asset under test plus
  configuration (known presets, budget cap, inheritance depth threshold).
- **Report** — severity-annotated entries (Error / Warning / Info) with
  `Passed()` (no errors), count helpers, and `ToJUnitXml()`.
- **Rules** — each implements `Check(ctx, report)`. The default set is
  returned by `VfxGetDefaultValidationRules()`.

## Baseline rules (8)

| # | Rule                        | Severity | Triggers when                                                 |
|---|----------------------------|----------|---------------------------------------------------------------|
| 1 | `BudgetViolation`          | Error    | `maxParticles` > `ctx.maxParticleBudget` (default 100 000)     |
| 2 | `EmptyModuleStack`         | Warning  | Graph has zero modules                                         |
| 3 | `NegativeLifespan`         | Error    | `lifespanDefault` ≤ 0                                          |
| 4 | `DuplicateParameters`      | Error    | Two parameters share the same name                             |
| 5 | `DuplicateEvents`          | Error    | Two events share the same name                                 |
| 6 | `UnresolvedPresetRef`      | Error    | VfxSystem emitter ref `preset:<Name>` not in `knownPresets`    |
| 7 | `SubEmitterRecursion`      | Error    | VfxSystem emitter ref points to the system's own path          |
| 8 | `ExcessiveInheritanceDepth`| Warning  | Parent chain deeper than `maxInheritanceDepthWarning` (default 4)|

## CI integration

```bash
# In your CI pipeline:
VfxValidate --content content/vfx --output vfx-report.xml
```

The CLI (future wrapper around `VfxValidateAsset`) scans every `.uzvfx` /
`.uzvfxsys` in the content directory, runs all rules, and writes
`ToJUnitXml()` output. CI runners ingest JUnit XML natively — errors appear
as test failures, warnings as annotations.

## Rule authoring

```cpp
struct MyCustomRule : IVfxValidationRule {
    const char* Name() const noexcept override { return "MyCustomRule"; }
    void Check(const VfxValidationContext& ctx,
               VfxValidationReport& report) const override {
        if (/* domain condition */)
            report.Add(VfxValidationSeverity::Error, Name(),
                       "human-readable message", ctx.assetPath);
    }
};
```

Register custom rules by appending to the vector returned by
`VfxGetDefaultValidationRules()` before passing it to the validation loop.

## Tests

`runtime/tests/engine/vfx/test_vfx_validation_rules.cpp` covers: empty
report passes, error fails report, warning doesn't fail, Clear resets,
JUnit XML `<failure>` for errors + `<system-out>` for warnings,
BudgetViolation trigger/pass, EmptyModuleStack warning, NegativeLifespan
trigger/pass, DuplicateParameters trigger, UnresolvedPresetRef trigger/pass,
SubEmitterRecursion trigger, default set has 8 rules, VfxValidateAsset
convenience with clean graph, severity name helper.
