/**
 * syntax-highlighting.test.ts — Unit tests for .uzscene TextMate grammar and
 * language configuration.
 *
 * These tests parse and validate the JSON files directly without requiring a
 * VSCode runtime.  They verify:
 *  - Grammar and language-config files exist and are valid JSON
 *  - package.json contributes the `uzscene` language and grammar
 *  - Grammar has the correct scopeName
 *  - Grammar repository contains patterns for sections, keys, numbers, comments
 *    and special node-type values
 */

import * as fs from 'fs';
import * as path from 'path';

// ---------------------------------------------------------------------------
// Path helpers
// ---------------------------------------------------------------------------

/** Absolute path to the vscode-uzengine package root. */
const PACKAGE_ROOT = path.resolve(__dirname, '..', '..');

function syntaxesPath(...parts: string[]): string {
    return path.join(PACKAGE_ROOT, 'syntaxes', ...parts);
}

function packageJsonPath(): string {
    return path.join(PACKAGE_ROOT, 'package.json');
}

// ---------------------------------------------------------------------------
// Typed interfaces for the artefacts we validate
// ---------------------------------------------------------------------------

interface TmLanguageRepository {
    [key: string]: {
        name?: string;
        match?: string;
        patterns?: unknown[];
        captures?: Record<string, unknown>;
    };
}

interface TmLanguage {
    scopeName: string;
    name: string;
    fileTypes: string[];
    patterns: unknown[];
    repository: TmLanguageRepository;
}

interface LanguageContribution {
    id: string;
    aliases: string[];
    extensions: string[];
    configuration: string;
}

interface GrammarContribution {
    language: string;
    scopeName: string;
    path: string;
}

interface PackageJsonContributes {
    languages?: LanguageContribution[];
    grammars?: GrammarContribution[];
}

interface PackageJson {
    contributes?: PackageJsonContributes;
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

function readJson<T>(filePath: string): T {
    const raw = fs.readFileSync(filePath, 'utf-8');
    return JSON.parse(raw) as T;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

describe('uzscene syntax highlighting artefacts', () => {

    // -----------------------------------------------------------------------
    // File existence + valid JSON
    // -----------------------------------------------------------------------

    describe('file existence and JSON validity', () => {
        it('uzscene.tmLanguage.json exists', () => {
            const p = syntaxesPath('uzscene.tmLanguage.json');
            expect(fs.existsSync(p)).toBe(true);
        });

        it('uzscene.tmLanguage.json is valid JSON', () => {
            const p = syntaxesPath('uzscene.tmLanguage.json');
            expect(() => readJson(p)).not.toThrow();
        });

        it('uzscene-language-config.json exists', () => {
            const p = syntaxesPath('uzscene-language-config.json');
            expect(fs.existsSync(p)).toBe(true);
        });

        it('uzscene-language-config.json is valid JSON', () => {
            const p = syntaxesPath('uzscene-language-config.json');
            expect(() => readJson(p)).not.toThrow();
        });
    });

    // -----------------------------------------------------------------------
    // package.json contributions
    // -----------------------------------------------------------------------

    describe('package.json contributions', () => {
        let pkg: PackageJson;

        beforeAll(() => {
            pkg = readJson<PackageJson>(packageJsonPath());
        });

        it('contributes.languages is an array', () => {
            expect(Array.isArray(pkg.contributes?.languages)).toBe(true);
        });

        it('contributes the uzscene language', () => {
            const lang = pkg.contributes?.languages?.find(l => l.id === 'uzscene');
            expect(lang).toBeDefined();
        });

        it('uzscene language has .uzscene extension', () => {
            const lang = pkg.contributes?.languages?.find(l => l.id === 'uzscene');
            expect(lang?.extensions).toContain('.uzscene');
        });

        it('uzscene language aliases include UzScene', () => {
            const lang = pkg.contributes?.languages?.find(l => l.id === 'uzscene');
            expect(lang?.aliases).toContain('UzScene');
        });

        it('uzscene language configuration points to the language-config file', () => {
            const lang = pkg.contributes?.languages?.find(l => l.id === 'uzscene');
            expect(lang?.configuration).toBe('./syntaxes/uzscene-language-config.json');
        });

        it('contributes.grammars is an array', () => {
            expect(Array.isArray(pkg.contributes?.grammars)).toBe(true);
        });

        it('contributes a grammar for the uzscene language', () => {
            const grammar = pkg.contributes?.grammars?.find(g => g.language === 'uzscene');
            expect(grammar).toBeDefined();
        });

        it('grammar scopeName is source.uzscene', () => {
            const grammar = pkg.contributes?.grammars?.find(g => g.language === 'uzscene');
            expect(grammar?.scopeName).toBe('source.uzscene');
        });

        it('grammar path points to the tmLanguage file', () => {
            const grammar = pkg.contributes?.grammars?.find(g => g.language === 'uzscene');
            expect(grammar?.path).toBe('./syntaxes/uzscene.tmLanguage.json');
        });
    });

    // -----------------------------------------------------------------------
    // TextMate grammar structure
    // -----------------------------------------------------------------------

    describe('uzscene.tmLanguage.json grammar structure', () => {
        let grammar: TmLanguage;

        beforeAll(() => {
            grammar = readJson<TmLanguage>(syntaxesPath('uzscene.tmLanguage.json'));
        });

        it('has the correct scopeName', () => {
            expect(grammar.scopeName).toBe('source.uzscene');
        });

        it('has a name field', () => {
            expect(typeof grammar.name).toBe('string');
            expect(grammar.name.length).toBeGreaterThan(0);
        });

        it('lists .uzscene in fileTypes', () => {
            expect(grammar.fileTypes).toContain('.uzscene');
        });

        it('has a top-level patterns array', () => {
            expect(Array.isArray(grammar.patterns)).toBe(true);
            expect(grammar.patterns.length).toBeGreaterThan(0);
        });

        it('has a repository object', () => {
            expect(grammar.repository).toBeDefined();
            expect(typeof grammar.repository).toBe('object');
        });

        // ---- section headers ----
        it('repository has a section pattern', () => {
            expect(grammar.repository.section).toBeDefined();
        });

        it('section pattern uses entity.name.section.uzscene scope', () => {
            const section = grammar.repository.section;
            // The scope may appear in `name` or in `captures`
            const scopeInName = section.name === 'entity.name.section.uzscene';
            const scopeInCaptures =
                JSON.stringify(section.captures ?? {}).includes('entity.name.section.uzscene');
            expect(scopeInName || scopeInCaptures).toBe(true);
        });

        it('section pattern match includes square-bracket syntax', () => {
            const section = grammar.repository.section;
            expect(section.match).toBeDefined();
            // The regex must include literal bracket characters
            expect(section.match).toMatch(/\\\[/);
        });

        // ---- key-value pairs ----
        it('repository has a keyvalue pattern', () => {
            expect(grammar.repository.keyvalue).toBeDefined();
        });

        it('keyvalue pattern captures a key with keyword.other.key.uzscene scope', () => {
            const kv = grammar.repository.keyvalue;
            const json = JSON.stringify(kv.captures ?? {});
            expect(json).toContain('keyword.other.key.uzscene');
        });

        it('keyvalue pattern captures the = separator with punctuation.separator.uzscene scope', () => {
            const kv = grammar.repository.keyvalue;
            const json = JSON.stringify(kv.captures ?? {});
            expect(json).toContain('punctuation.separator.uzscene');
        });

        // ---- numeric values ----
        it('repository has a number pattern', () => {
            expect(grammar.repository.number).toBeDefined();
        });

        it('number pattern uses constant.numeric.uzscene scope', () => {
            const num = grammar.repository.number;
            expect(num.name).toBe('constant.numeric.uzscene');
        });

        it('number pattern match handles floats', () => {
            const num = grammar.repository.number;
            expect(num.match).toBeDefined();
            const re = new RegExp(num.match as string);
            expect(re.test('3.14')).toBe(true);
            expect(re.test('0.000000')).toBe(true);
            expect(re.test('42')).toBe(true);
        });

        it('number pattern match handles negative numbers', () => {
            const num = grammar.repository.number;
            const re = new RegExp(num.match as string);
            expect(re.test('-1.5')).toBe(true);
        });

        // ---- comments ----
        it('repository has a comment pattern', () => {
            expect(grammar.repository.comment).toBeDefined();
        });

        it('comment pattern uses comment.line.uzscene scope', () => {
            const comment = grammar.repository.comment;
            expect(comment.name).toBe('comment.line.uzscene');
        });

        it('comment pattern match handles # prefix', () => {
            const comment = grammar.repository.comment;
            expect(comment.match).toBeDefined();
            const re = new RegExp(comment.match as string);
            expect(re.test('# this is a comment')).toBe(true);
        });

        it('comment pattern match handles ; prefix', () => {
            const comment = grammar.repository.comment;
            const re = new RegExp(comment.match as string);
            expect(re.test('; another comment')).toBe(true);
        });

        // ---- node-type values ----
        it('repository has a node-type pattern', () => {
            expect(grammar.repository['node-type']).toBeDefined();
        });

        it('node-type pattern uses support.type.node-type.uzscene scope', () => {
            const nt = grammar.repository['node-type'];
            expect(nt.name).toBe('support.type.node-type.uzscene');
        });

        it('node-type match includes Node3D', () => {
            const nt = grammar.repository['node-type'];
            expect(nt.match).toContain('Node3D');
        });

        it('node-type match includes all required node types', () => {
            const nt = grammar.repository['node-type'];
            const required = [
                'Empty', 'Node3D', 'StaticMesh', 'DirectionalLight',
                'PointLight', 'SpotLight', 'Camera', 'AudioSource', 'AudioListener',
            ];
            for (const nodeType of required) {
                expect(nt.match).toContain(nodeType);
            }
        });

        it('node-type regex matches known node type values', () => {
            const nt = grammar.repository['node-type'];
            const re = new RegExp(nt.match as string);
            const knownTypes = [
                'Empty', 'Node3D', 'StaticMesh', 'DirectionalLight',
                'PointLight', 'SpotLight', 'Camera', 'AudioSource', 'AudioListener',
            ];
            for (const t of knownTypes) {
                expect(re.test(t)).toBe(true);
            }
        });

        it('node-type regex does not match unknown values', () => {
            const nt = grammar.repository['node-type'];
            const re = new RegExp(`^${nt.match as string}$`);
            expect(re.test('Unknown')).toBe(false);
            expect(re.test('Mesh')).toBe(false);
        });

        // ---- string value fallback ----
        it('repository has a string-value pattern', () => {
            expect(grammar.repository['string-value']).toBeDefined();
        });

        it('string-value pattern uses string.unquoted.value.uzscene scope', () => {
            const sv = grammar.repository['string-value'];
            expect(sv.name).toBe('string.unquoted.value.uzscene');
        });
    });

    // -----------------------------------------------------------------------
    // Language configuration structure
    // -----------------------------------------------------------------------

    describe('uzscene-language-config.json structure', () => {
        let config: {
            comments: { lineComment: string };
            brackets: string[][];
            autoClosingPairs: string[][];
            wordPattern: string;
        };

        beforeAll(() => {
            config = readJson(syntaxesPath('uzscene-language-config.json'));
        });

        it('has a comments.lineComment set to #', () => {
            expect(config.comments?.lineComment).toBe('#');
        });

        it('has brackets containing ["[", "]"]', () => {
            expect(Array.isArray(config.brackets)).toBe(true);
            const hasBracketPair = config.brackets.some(
                pair => pair[0] === '[' && pair[1] === ']'
            );
            expect(hasBracketPair).toBe(true);
        });

        it('has autoClosingPairs containing ["[", "]"]', () => {
            expect(Array.isArray(config.autoClosingPairs)).toBe(true);
            const hasPair = config.autoClosingPairs.some(
                pair => pair[0] === '[' && pair[1] === ']'
            );
            expect(hasPair).toBe(true);
        });

        it('has a wordPattern string', () => {
            expect(typeof config.wordPattern).toBe('string');
            expect(config.wordPattern.length).toBeGreaterThan(0);
        });

        it('wordPattern matches valid identifiers', () => {
            const re = new RegExp(config.wordPattern);
            expect(re.test('name')).toBe(true);
            expect(re.test('position')).toBe(true);
            expect(re.test('Node3D')).toBe(true);
            expect(re.test('_private')).toBe(true);
        });
    });
});
