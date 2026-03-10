/// Vitest configuration for UzEngine MCP server tests.
///
/// Configures the test runner with strict coverage thresholds and Node.js environment.
/// All metrics must maintain 95%+ coverage per project requirements.

import { defineConfig } from 'vitest/config';

/// Vitest configuration.
///
/// # Test Configuration
/// - Environment: Node.js (no DOM)
/// - Globals: Test functions available without import
/// - Test Files: `tests/**/*.test.ts`
/// - Pass on empty: Allows configs to pass even with no tests
///
/// # Coverage Requirements (95% threshold)
/// All of the following must achieve ≥95% coverage:
/// - Statements: 95%
/// - Branches: 95%
/// - Functions: 95%
/// - Lines: 95%
///
/// Provider: V8 (Node.js native coverage)
export default defineConfig({
  test: {
    globals: true,
    environment: 'node',
    include: ['tests/**/*.test.ts'],
    passWithNoTests: true,
    coverage: {
      provider: 'v8',
      include: ['src/**/*.ts'],
      thresholds: {
        statements: 95,
        branches: 95,
        functions: 95,
        lines: 95,
      },
    },
  },
});
