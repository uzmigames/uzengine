<!-- doc-meta
title: MCP Auto-Documentation Generator
category: mcp
theme: tooling
created: 2026-03-25
updated: 2026-03-25
status: planning
author: UzEngine Team
-->

# MCP Auto-Documentation Generator

> **Goal**: Parse Rust-style `///` doc comments from the entire codebase (C++, HLSL, TypeScript) and generate a structured `docs.json` that powers MCP search tools for LLM-assisted development.

---

## Problem

LLMs working on UzEngine need to understand:
- What each class/struct/function/shader does
- How subsystems connect (ECS component -> renderer -> shader)
- What parameters exist, their types, defaults, and ranges
- Academic references (`@ref`) behind rendering techniques
- Code examples embedded in doc comments

Currently this knowledge is scattered across ~5,200 `///` comment lines in 115+ files. An LLM must grep/read individual files to find it. A structured JSON index enables instant semantic search.

---

## Architecture

```
┌─────────────────────────────────────────┐
│  mcp/tools/generate-docs.ts             │  ← TypeScript script (Node.js)
│                                         │
│  1. Glob all .h, .cpp, .hlsl, .hlsli    │
│  2. Parse /// doc blocks                │
│  3. Extract entities + metadata         │
│  4. Resolve cross-references            │
│  5. Write docs.json                     │
└──────────────┬──────────────────────────┘
               │
               ▼
┌─────────────────────────────────────────┐
│  mcp/data/docs.json                     │  ← Generated output
│                                         │
│  Array of DocumentedEntity:             │
│  - id, name, kind, file, line           │
│  - description, sections                │
│  - params, returns, examples            │
│  - tags, refs, relationships            │
└──────────────┬──────────────────────────┘
               │
               ▼
┌─────────────────────────────────────────┐
│  MCP Server Tool: search_docs           │  ← Consumed by LLMs
│                                         │
│  - Full-text search on descriptions     │
│  - Filter by kind, tags, subsystem      │
│  - Traverse relationships               │
│  - Find by @ref paper citation          │
└─────────────────────────────────────────┘
```

---

## Phase 1 — Parser (`mcp/tools/generate-docs.ts`)

### Input Sources

| Source | Pattern | Count (approx) |
|--------|---------|-----------------|
| C++ headers | `runtime/**/*.h` | ~180 files |
| C++ source | `runtime/**/*.cpp` | ~150 files |
| HLSL shaders | `runtime/shaders/**/*.hlsl` | ~80 files |
| HLSL includes | `runtime/shaders/**/*.hlsli` | ~30 files |
| TypeScript | `mcp/src/**/*.ts`, `extension/src/**/*.ts` | ~10 files |

### Doc Comment Syntax to Parse

```
/// Single-line doc comment
///< Trailing member doc comment

/// Multi-line block:
/// First line is the brief description.
///
/// Extended description paragraph(s).
///
/// # Section Header
/// Section content.
///
/// # Arguments
/// * `param_name` - Description of parameter
///
/// # Returns
/// Description of return value.
///
/// # Example
/// ```cpp
/// auto x = MyFunc(42);
/// ```
///
/// @ref Author Year "Paper Title" — technique description
/// @see OtherClass, OtherFunction
```

### Entity Types to Extract

| Kind | C++ Pattern | Example |
|------|-------------|---------|
| `class` | `class Foo {` / `class Foo final : public Bar {` | `ISubsystem`, `TimeManager` |
| `struct` | `struct Foo {` | `ImportedMaterial`, `AtmosphereFrameContext` |
| `enum` | `enum class Foo` | `InitPhase`, `DebugViewMode` |
| `function` | return type + name + `(` | `BeginFrame()`, `ConsumeFixedTicks()` |
| `method` | same, inside class/struct scope | `TimeManager::SetFixedDeltaTime()` |
| `field` | type + name inside struct/class | `float fogDensity = 0.02f` |
| `macro` | `#define UZ_*` | `UZ_LOG`, `UZ_CHECK` |
| `shader_fn` | HLSL function definitions | `GetExponentialFogColor()` |
| `shader_cb` | `cbuffer` blocks | `PerFrameCB`, `MaterialCB` |
| `file` | file-level `///` block (before any declaration) | Module-level docs |

### Metadata Extraction

For each entity, extract:

```typescript
interface DocumentedEntity {
  /// Unique identifier: "file_path::entity_name"
  id: string;

  /// Human-readable name
  name: string;

  /// Entity kind
  kind: "file" | "class" | "struct" | "enum" | "enum_value" | "function"
      | "method" | "field" | "macro" | "shader_fn" | "shader_cb" | "typedef";

  /// Source location
  file: string;      // Relative path from repo root
  line: number;      // Line number of the entity declaration
  lineEnd?: number;  // End line (for multi-line entities)

  /// Documentation content
  brief: string;              // First paragraph (before first blank line)
  description?: string;       // Full extended description
  sections: Record<string, string>; // Named sections: "Design", "Thread Safety", etc.

  /// Structured metadata
  params?: ParamDoc[];        // # Arguments entries
  returns?: string;           // # Returns content
  examples?: string[];        // # Example code blocks
  refs?: RefDoc[];            // @ref citations
  seeAlso?: string[];         // @see cross-references

  /// Classification
  tags: string[];             // Auto-generated: subsystem, layer, language
  subsystem: string;          // Derived from file path (renderer, ecs, rhi, etc.)
  language: "cpp" | "hlsl" | "typescript";

  /// Relationships (resolved in Phase 2)
  relationships: Relationship[];

  /// For fields/params: type info
  type?: string;              // C++ type as string
  defaultValue?: string;      // Default value if present
}

interface ParamDoc {
  name: string;
  description: string;
  type?: string;
}

interface RefDoc {
  author: string;
  year?: string;
  title: string;
  note?: string;              // Text after "—"
}

interface Relationship {
  kind: "inherits" | "contains" | "uses" | "implements" | "references"
      | "binds_to" | "reads_from" | "writes_to";
  target: string;             // Entity ID or name
  description?: string;
}
```

### Subsystem Derivation

Map file paths to subsystems automatically:

```typescript
const SUBSYSTEM_MAP: Record<string, string> = {
  "engine/app/":                 "application",
  "engine/ecs/":                 "ecs",
  "engine/ecs/environment/":     "environment",
  "engine/renderer/":            "renderer",
  "engine/renderer/rhi/":        "rhi",
  "engine/renderer/passes/":     "render-passes",
  "engine/renderer/atmosphere/": "atmosphere",
  "engine/renderer/core/":       "renderer-core",
  "engine/shader/":              "shader-system",
  "engine/asset/":               "assets",
  "engine/scripting/":           "scripting",
  "engine/math/":                "math",
  "engine/job/":                 "job-system",
  "engine/profiling/":           "profiling",
  "engine/ipc/":                 "ipc",
  "editor/":                     "editor",
  "platform/":                   "platform",
  "shaders/runtime/":            "shaders",
  "shaders/editor/":             "editor-shaders",
  "mcp/":                        "mcp",
  "extension/":                  "vscode-extension",
};
```

### Tag Generation

Auto-tags derived from context:

| Tag | When Applied |
|-----|--------------|
| `component` | Struct/class inside `ecs/` or name ends with `Component` |
| `pass` | Class inside `passes/` or name ends with `Pass` |
| `rhi` | Anything inside `rhi/` |
| `shader` | Any `.hlsl` or `.hlsli` entity |
| `cbuffer` | HLSL `cbuffer` blocks |
| `subsystem` | Implements `ISubsystem` |
| `public-api` | Documented with `///` + is in a `.h` file |
| `internal` | In `.cpp` file only (not exposed in header) |
| `rendering` | Inside renderer/, shaders/, or atmosphere/ |
| `editor` | Inside editor/ |
| `reference` | Has `@ref` citation |
| `unsafe` | Contains raw pointers, GPU resources, or threading notes |

---

## Phase 2 — Relationship Resolution

After all entities are parsed, resolve cross-references:

### Automatic Relationships

1. **Inheritance**: Parse `: public Base` → `inherits` relationship
2. **Composition**: Fields whose type is another documented entity → `contains`
3. **`@see` links**: Resolve to entity IDs → `references`
4. **Include graph**: `#include "foo.h"` → file-level `uses` relationship
5. **CB ↔ Shader**: Match `cbuffer PerFrameCB` in HLSL with `struct PerFrameCB` in C++ → `binds_to`
6. **Component ↔ Renderer**: Components referenced in renderer files → `reads_from`
7. **Shader ↔ Pass**: Shaders loaded by pass classes → `uses`

### Heuristic Relationships

For cases where explicit annotations don't exist, use naming conventions:

- `*_pass.h` + `*_ps.hlsl` sharing a prefix → pass `uses` shader
- `*_component.h` field appearing in `*_renderer.cpp` → renderer `reads_from` component
- `*CB` struct in C++ matching `cbuffer *CB` in HLSL → `binds_to`

---

## Phase 3 — MCP Tool (`search_docs`)

### Tool Definition

```typescript
server.tool("search_docs", {
  query: z.string().describe("Search term or natural language query"),
  kind: z.enum(["class", "struct", "enum", "function", "method",
                 "field", "macro", "shader_fn", "shader_cb", "file"])
         .optional()
         .describe("Filter by entity kind"),
  subsystem: z.string().optional().describe("Filter by subsystem name"),
  tags: z.array(z.string()).optional().describe("Filter by tags (AND logic)"),
  ref: z.string().optional().describe("Search by @ref paper citation"),
  related_to: z.string().optional().describe("Find entities related to this entity"),
  limit: z.number().default(10).describe("Max results to return"),
}, async ({ query, kind, subsystem, tags, ref, related_to, limit }) => {
  // Load docs.json, search, filter, rank, return
});
```

### Search Strategy

1. **Exact match**: Entity name matches query exactly → highest rank
2. **Prefix match**: Entity name starts with query → high rank
3. **Brief match**: Query words appear in brief description → medium rank
4. **Full-text**: Query words in full description/sections → lower rank
5. **Tag match**: Query matches a tag → boost
6. **Ref match**: Query matches `@ref` author/title → boost

### Tool Response Format

```json
{
  "results": [
    {
      "id": "runtime/engine/app/time_manager.h::TimeManager",
      "name": "TimeManager",
      "kind": "class",
      "file": "runtime/engine/app/time_manager.h",
      "line": 25,
      "brief": "Tracks frame timing, provides scaled delta time, and implements a fixed-timestep accumulator.",
      "subsystem": "application",
      "tags": ["public-api", "subsystem"],
      "relationships": [
        { "kind": "implements", "target": "ISubsystem" }
      ],
      "relevance": 0.95
    }
  ],
  "total": 1,
  "query": "time manager"
}
```

### Additional MCP Tools

| Tool | Purpose |
|------|---------|
| `search_docs` | Full-text + filtered search |
| `get_entity` | Get full doc for a specific entity by ID |
| `get_relationships` | Get all entities related to a given entity |
| `get_subsystem_map` | List all subsystems with entity counts |
| `get_data_flow` | Trace a field from component → CB → shader |
| `get_refs` | List all academic references in the codebase |

---

## Phase 4 — Incremental Updates

The generator should support incremental mode for fast updates:

```bash
# Full generation (first run or after major changes)
npx ts-node mcp/tools/generate-docs.ts

# Incremental (only files changed since last run)
npx ts-node mcp/tools/generate-docs.ts --incremental

# Watch mode (re-generate on file save)
npx ts-node mcp/tools/generate-docs.ts --watch
```

Implementation:
- Store file mtimes in `mcp/data/.docs-meta.json`
- On incremental run, only re-parse changed files
- Merge updated entities into existing `docs.json`
- Re-resolve relationships that touch changed files

---

## File Structure

```
mcp/
├── src/
│   ├── server.ts              ← Existing MCP server
│   └── logger.ts              ← Existing logger
├── tools/
│   ├── generate-docs.ts       ← Main generator script
│   ├── parsers/
│   │   ├── cpp-parser.ts      ← C++ header/source parser
│   │   ├── hlsl-parser.ts     ← HLSL/HLSLI shader parser
│   │   └── ts-parser.ts       ← TypeScript parser
│   ├── resolvers/
│   │   ├── relationship-resolver.ts  ← Cross-reference resolution
│   │   ├── tag-generator.ts          ← Auto-tag generation
│   │   └── subsystem-mapper.ts       ← Path → subsystem mapping
│   └── types.ts               ← Shared TypeScript interfaces
├── data/
│   ├── docs.json              ← Generated output (gitignored)
│   └── .docs-meta.json        ← File mtimes for incremental mode
├── package.json
└── tsconfig.json
```

---

## Implementation Plan

### Step 1: Scaffold + Types
- Create `mcp/tools/` directory structure
- Define TypeScript interfaces in `types.ts`
- Add npm scripts to `mcp/package.json`

### Step 2: C++ Parser
- Parse `///` and `///<` comment blocks
- Extract entity declarations (class, struct, enum, function, field)
- Parse sections (`# Arguments`, `# Returns`, `# Example`, etc.)
- Parse `@ref` and `@see` annotations
- Parse default values from field declarations

### Step 3: HLSL Parser
- Reuse comment parsing from C++ parser
- Add `cbuffer` block extraction
- Add shader function extraction (different syntax from C++)
- Map `SV_Target`, `SV_Position` semantics

### Step 4: Relationship Resolver
- Inheritance graph from `: public Base`
- Include graph from `#include`
- CB binding: match C++ structs with HLSL cbuffers
- Component → renderer data flow
- `@see` resolution

### Step 5: MCP Tool Integration
- Add `search_docs` tool to MCP server
- Add `get_entity` tool
- Add `get_relationships` tool
- Load `docs.json` at server startup
- Build in-memory search index

### Step 6: Incremental + Watch
- File mtime tracking
- Partial re-parse
- Watch mode via `chokidar`

---

## Quality Criteria

- Parses 100% of `///` doc blocks without data loss
- Correctly identifies all entity kinds
- Resolves 90%+ of cross-references automatically
- `search_docs` returns relevant results in < 50ms
- `docs.json` < 5 MB for current codebase size
- Zero false positives in relationship resolution (prefer missing over wrong)

---

## Non-Goals (Phase 1)

- HTML/web documentation output (future: generate from same JSON)
- Markdown doc generation (already have manual docs in `docs/`)
- LSP hover integration (future: serve from same JSON)
- Automatic doc comment linting (separate tool)
