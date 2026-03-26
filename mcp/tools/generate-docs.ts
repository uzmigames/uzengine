#!/usr/bin/env node
/// MCP Auto-Documentation Generator entry point.
///
/// Globs all source files, runs appropriate parsers, resolves relationships,
/// generates tags and subsystem mappings, and writes docs.json.

import { glob } from "glob";
import { readFileSync, writeFileSync, existsSync, statSync } from "node:fs";
import { resolve, relative } from "node:path";

import type { DocumentedEntity, DocsDatabase, DocsMeta } from "./types.js";
import { parseCppFile } from "./parsers/cpp-parser.js";
import { parseHlslFile } from "./parsers/hlsl-parser.js";
import { parseTsFile } from "./parsers/ts-parser.js";
import { resolveRelationships } from "./resolvers/relationship-resolver.js";
import { mapSubsystem } from "./resolvers/subsystem-mapper.js";
import { generateTags } from "./resolvers/tag-generator.js";

// ─── Configuration ─────────────────────────────────────────────

const REPO_ROOT = resolve(import.meta.dirname ?? ".", "../..");
const OUTPUT_DIR = resolve(REPO_ROOT, "mcp", "data");
const DOCS_JSON = resolve(OUTPUT_DIR, "docs.json");
const META_JSON = resolve(OUTPUT_DIR, ".docs-meta.json");

const SOURCE_PATTERNS = [
  "runtime/engine/**/*.h",
  "runtime/engine/**/*.cpp",
  "runtime/editor/**/*.h",
  "runtime/editor/**/*.cpp",
  "runtime/platform/**/*.h",
  "runtime/platform/**/*.cpp",
  "runtime/demos/**/*.h",
  "runtime/demos/**/*.cpp",
  "runtime/tools/**/*.cpp",
  "runtime/shaders/**/*.hlsl",
  "runtime/shaders/**/*.hlsli",
  "mcp/src/**/*.ts",
  "extension/src/**/*.ts",
];

const IGNORE_PATTERNS = [
  "**/node_modules/**",
  "**/thirdparty/**",
  "**/build/**",
  "**/.zig-cache/**",
  "**/dist/**",
  "**/tests/**",
];

// ─── File Discovery ────────────────────────────────────────────

async function discoverFiles(): Promise<string[]> {
  const files: string[] = [];
  for (const pattern of SOURCE_PATTERNS) {
    const matches = await glob(pattern, {
      cwd: REPO_ROOT,
      ignore: IGNORE_PATTERNS,
      posix: true,
    });
    files.push(...matches);
  }
  // Deduplicate and sort
  return [...new Set(files)].sort();
}

// ─── Incremental Mode ──────────────────────────────────────────

function loadMeta(): DocsMeta | null {
  if (!existsSync(META_JSON)) return null;
  try {
    return JSON.parse(readFileSync(META_JSON, "utf-8"));
  } catch {
    return null;
  }
}

function saveMeta(files: string[]): void {
  const meta: DocsMeta = {
    version: 1,
    generatedAt: new Date().toISOString(),
    files: files.map((f) => ({
      path: f,
      mtimeMs: statSync(resolve(REPO_ROOT, f)).mtimeMs,
    })),
  };
  writeFileSync(META_JSON, JSON.stringify(meta, null, 2));
}

function getChangedFiles(
  files: string[],
  meta: DocsMeta,
): string[] {
  const oldMap = new Map(meta.files.map((f) => [f.path, f.mtimeMs]));
  return files.filter((f) => {
    const oldMtime = oldMap.get(f);
    if (oldMtime === undefined) return true; // New file
    const currentMtime = statSync(resolve(REPO_ROOT, f)).mtimeMs;
    return currentMtime > oldMtime;
  });
}

// ─── Parsing ───────────────────────────────────────────────────

function parseFile(
  filePath: string,
): DocumentedEntity[] {
  const fullPath = resolve(REPO_ROOT, filePath);
  const source = readFileSync(fullPath, "utf-8");

  if (filePath.endsWith(".ts")) {
    return parseTsFile(source, filePath);
  }
  if (filePath.endsWith(".hlsl") || filePath.endsWith(".hlsli")) {
    return parseHlslFile(source, filePath);
  }
  // .h or .cpp
  return parseCppFile(source, filePath);
}

// ─── Post-Processing ───────────────────────────────────────────

function postProcess(entities: DocumentedEntity[]): void {
  // Apply subsystem mapping and auto-tags
  for (const entity of entities) {
    entity.subsystem = mapSubsystem(entity.file);
    entity.tags = generateTags(entity);
  }

  // Resolve cross-references
  resolveRelationships(entities);
}

// ─── Main ──────────────────────────────────────────────────────

async function main(): Promise<void> {
  const startTime = performance.now();
  const args = process.argv.slice(2);
  const incremental = args.includes("--incremental");
  const watchMode = args.includes("--watch");

  console.log(`[generate-docs] Starting${incremental ? " (incremental)" : ""}...`);

  // Discover source files
  const allFiles = await discoverFiles();
  console.log(`[generate-docs] Found ${allFiles.length} source files`);

  // Determine which files to parse
  let filesToParse: string[];
  let existingEntities: DocumentedEntity[] = [];

  if (incremental) {
    const meta = loadMeta();
    if (meta && existsSync(DOCS_JSON)) {
      const changed = getChangedFiles(allFiles, meta);
      if (changed.length === 0) {
        console.log("[generate-docs] No changes detected, skipping.");
        return;
      }
      console.log(`[generate-docs] ${changed.length} files changed`);
      filesToParse = changed;

      // Load existing entities, remove stale ones from changed files
      const db: DocsDatabase = JSON.parse(readFileSync(DOCS_JSON, "utf-8"));
      const changedSet = new Set(changed);
      existingEntities = db.entities.filter((e) => !changedSet.has(e.file));
    } else {
      filesToParse = allFiles;
    }
  } else {
    filesToParse = allFiles;
  }

  // Parse all files
  const newEntities: DocumentedEntity[] = [];
  let parseErrors = 0;

  for (const file of filesToParse) {
    try {
      const entities = parseFile(file);
      newEntities.push(...entities);
    } catch (err) {
      parseErrors++;
      console.error(`[generate-docs] Error parsing ${file}:`, err);
    }
  }

  // Merge with existing (for incremental)
  const allEntities = [...existingEntities, ...newEntities];

  // Post-process: subsystem, tags, relationships
  postProcess(allEntities);

  // Sort deterministically: by file path, then line number
  allEntities.sort((a, b) => {
    const fc = a.file.localeCompare(b.file);
    if (fc !== 0) return fc;
    return a.line - b.line;
  });

  // Write docs.json
  const db: DocsDatabase = {
    version: 1,
    generatedAt: new Date().toISOString(),
    fileCount: allFiles.length,
    entities: allEntities,
  };

  // Ensure output directory exists
  const { mkdirSync } = await import("node:fs");
  mkdirSync(OUTPUT_DIR, { recursive: true });

  writeFileSync(DOCS_JSON, JSON.stringify(db, null, 2));
  saveMeta(allFiles);

  const elapsed = ((performance.now() - startTime) / 1000).toFixed(2);
  const sizeKb = (Buffer.byteLength(JSON.stringify(db)) / 1024).toFixed(0);

  console.log(`[generate-docs] Done in ${elapsed}s`);
  console.log(`[generate-docs] ${allEntities.length} entities from ${allFiles.length} files`);
  console.log(`[generate-docs] docs.json: ${sizeKb} KB`);
  if (parseErrors > 0) {
    console.warn(`[generate-docs] ${parseErrors} files had parse errors`);
  }

  // Watch mode
  if (watchMode) {
    const { watch } = await import("chokidar");
    const watchPaths = SOURCE_PATTERNS.map((p) => resolve(REPO_ROOT, p));
    console.log("[generate-docs] Watching for changes...");

    const watcher = watch(watchPaths, {
      ignored: IGNORE_PATTERNS,
      ignoreInitial: true,
      awaitWriteFinish: { stabilityThreshold: 300 },
    });

    watcher.on("change", async (changedPath: string) => {
      const rel = relative(REPO_ROOT, changedPath).replace(/\\/g, "/");
      console.log(`[generate-docs] File changed: ${rel}`);
      // Re-run with incremental
      process.argv.push("--incremental");
      await main();
    });
  }
}

main().catch((err) => {
  console.error("[generate-docs] Fatal error:", err);
  process.exit(1);
});
