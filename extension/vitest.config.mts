import { defineConfig } from 'vitest/config';

export default defineConfig({
  test: {
    globals: true,
    environment: 'node',
    // src/tests/*-test.ts are manual native/editor harnesses, not Vitest suites.
    include: ['tests/**/*.test.ts', 'src/**/*.test.ts'],
    passWithNoTests: false,
    coverage: {
      provider: 'v8',
      include: ['src/**/*.ts'],
      // Standalone native/editor diagnostic programs are consumers, not shipped extension code.
      exclude: ['src/tests/**', 'src/**/*.d.ts'],
      thresholds: {
        statements: 95,
        branches: 95,
        functions: 95,
        lines: 95,
      },
    },
  },
});
