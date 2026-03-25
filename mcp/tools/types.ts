/// Shared type definitions for the MCP auto-documentation generator.
///
/// These interfaces define the schema for parsed doc entities,
/// relationships, search results, and the docs.json output format.

// ─── Entity Types ──────────────────────────────────────────────

/// All recognized entity kinds extracted from source files.
export type EntityKind =
  | "file"
  | "class"
  | "struct"
  | "enum"
  | "enum_value"
  | "function"
  | "method"
  | "field"
  | "macro"
  | "shader_fn"
  | "shader_cb"
  | "typedef";

/// Source language of the entity.
export type Language = "cpp" | "hlsl" | "typescript";

// ─── Documentation Metadata ────────────────────────────────────

/// A parsed `@ref Author Year "Title" — note` citation.
export interface RefDoc {
  author: string;
  year?: string;
  title: string;
  note?: string;
}

/// A parsed parameter from `# Arguments` sections.
export interface ParamDoc {
  name: string;
  description: string;
  type?: string;
}

// ─── Relationships ─────────────────────────────────────────────

/// Relationship kinds between documented entities.
export type RelationshipKind =
  | "inherits"
  | "contains"
  | "uses"
  | "implements"
  | "references"
  | "binds_to"
  | "reads_from"
  | "writes_to";

/// A directed relationship from the owning entity to a target.
export interface Relationship {
  kind: RelationshipKind;
  target: string;
  description?: string;
}

// ─── Core Entity ───────────────────────────────────────────────

/// A single documented code entity extracted from source.
///
/// This is the primary data structure stored in docs.json.
/// Each entity represents one class, struct, function, shader, etc.
export interface DocumentedEntity {
  /// Unique identifier: "relative/file/path.h::EntityName"
  id: string;

  /// Human-readable name (e.g., "TimeManager", "fogDensity", "D_GGX")
  name: string;

  /// Entity kind
  kind: EntityKind;

  /// Source file path relative to repo root
  file: string;

  /// Line number of the entity declaration (1-based)
  line: number;

  /// End line number for multi-line entities
  lineEnd?: number;

  /// First paragraph of the doc comment (before first blank line)
  brief: string;

  /// Full extended description (all paragraphs after the brief)
  description?: string;

  /// Named sections extracted from `# Heading` markers.
  /// Keys: "Design", "Thread Safety", "Behavior", "Ownership", etc.
  sections: Record<string, string>;

  /// Parsed `# Arguments` entries
  params?: ParamDoc[];

  /// Parsed `# Returns` content
  returns?: string;

  /// Code blocks from `# Example` sections
  examples?: string[];

  /// Parsed `@ref` citations
  refs?: RefDoc[];

  /// Parsed `@see` cross-references (raw names, resolved later)
  seeAlso?: string[];

  /// Auto-generated classification tags
  tags: string[];

  /// Subsystem derived from file path
  subsystem: string;

  /// Source language
  language: Language;

  /// Resolved cross-references (populated by relationship resolver)
  relationships: Relationship[];

  /// C++ / HLSL type as string (for fields, params, return types)
  type?: string;

  /// Default value if present in declaration (e.g., "0.02f")
  defaultValue?: string;

  /// Parent entity ID (for methods inside classes, fields inside structs)
  parent?: string;
}

// ─── Generator Output ──────────────────────────────────────────

/// The top-level structure of docs.json.
export interface DocsDatabase {
  /// Schema version for forward compatibility
  version: number;

  /// ISO 8601 timestamp of generation
  generatedAt: string;

  /// Total number of source files processed
  fileCount: number;

  /// All documented entities, sorted by file path then line number
  entities: DocumentedEntity[];
}

// ─── Search Types ──────────────────────────────────────────────

/// A search result returned by the MCP search_docs tool.
export interface SearchResult {
  id: string;
  name: string;
  kind: EntityKind;
  file: string;
  line: number;
  brief: string;
  subsystem: string;
  tags: string[];
  relationships: Relationship[];
  relevance: number;
}

/// Filters for search_docs queries.
export interface SearchFilters {
  kind?: EntityKind;
  subsystem?: string;
  tags?: string[];
}

// ─── Incremental Mode ──────────────────────────────────────────

/// File metadata for incremental generation.
export interface FileMetadata {
  path: string;
  mtimeMs: number;
  hash?: string;
}

/// Stored in mcp/data/.docs-meta.json for incremental mode.
export interface DocsMeta {
  version: number;
  generatedAt: string;
  files: FileMetadata[];
}
