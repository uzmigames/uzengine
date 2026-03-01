// @ts-check
const tseslint = require('typescript-eslint');

module.exports = tseslint.config(
    // Ignore compiled output and deps
    { ignores: ['out/**', 'node_modules/**', '**/*.d.ts'] },

    // TypeScript source files
    ...tseslint.configs.recommendedTypeChecked,
    {
        languageOptions: {
            parserOptions: {
                project: './tsconfig.json',
                tsconfigRootDir: __dirname,
            },
        },
        rules: {
            '@typescript-eslint/naming-convention': [
                'warn',
                { selector: 'import', format: ['camelCase', 'PascalCase'] },
            ],
            'curly': 'warn',
            'eqeqeq': ['warn', 'always'],
            'no-throw-literal': 'off',
            '@typescript-eslint/only-throw-error': 'warn',
            'semi': 'off',
            '@typescript-eslint/semi': ['warn', 'always'],
        },
    }
);
