# Unit Tests Only for Rendering Validation

**Category**: testing
**Tags**: testing, rendering, visual-validation, screenshot, demo

## Description

Relying solely on unit tests to validate rendering features. Math tests and API tests can all pass green while the actual GPU output is a black screen, wrong colors, or broken pipeline. The only real validation for shaders, render passes, and post-process effects is seeing the pixels via demo + screenshot.

## Example

// WRONG: Marking task complete because tests pass\nTEST(TransmittanceLUT, Values) {\n    EXPECT_NEAR(lut[0], 0.97f, 0.01f); // Passes! But GPU output is black\n}\n\n// CORRECT: Demo + screenshot verification\n// 1. Build and run demo with --screenshot\n// 2. View screenshot (Read tool on PNG)\n// 3. Verify visual result matches expected output\n// 4. Delete screenshot after verification\n// 5. THEN mark task complete

## When to Use

When reviewing test coverage for rendering features. When someone marks a rendering task complete based only on unit tests.
