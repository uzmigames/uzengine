# Complete Implementation — Research First, Implement Fully

**Category**: code
**Tags**: quality, implementation, complete, research-first, critical

## Description

Before writing any code, research the correct approach (reference engines, codebase patterns, specs). Then implement the full solution: every function body, every edge case, every error path. No shortcuts. If the implementation is 500 lines, write 500 lines. If it handles 10 cases, handle all 10.

## Example

// ✅ CORRECT — full implementation, no matter how complex
float CalculateFresnelSchlick(float cosTheta, float f0) {
    float oneMinusCos = 1.0f - cosTheta;
    float oneMinusCos2 = oneMinusCos * oneMinusCos;
    float oneMinusCos5 = oneMinusCos2 * oneMinusCos2 * oneMinusCos;
    return f0 + (1.0f - f0) * oneMinusCos5;
}

// ✅ CORRECT — if task is too large, communicate instead of simplifying
// "This system requires ~3000 lines across 5 files. I propose implementing
//  in 3 phases: Phase 1 (core data structures), Phase 2 (algorithms),
//  Phase 3 (integration). Shall I proceed with Phase 1?"

## When to Use

Always. Every implementation task must follow this pattern.

## When NOT to Use

Never skip this. There are no exceptions to the quality-over-speed rule.
