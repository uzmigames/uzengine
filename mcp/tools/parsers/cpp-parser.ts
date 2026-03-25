/// C++ doc comment parser.
///
/// Extracts documented entities (classes, structs, enums, functions, fields,
/// macros) from C++ header and source files by parsing `///` and `///<`
/// comment blocks and associating them with the following declaration.

import type {
  DocumentedEntity,
  EntityKind,
  ParamDoc,
  RefDoc,
} from "../types.js";

// ─── Doc Block Extraction ──────────────────────────────────────

interface RawDocBlock {
  /// The combined doc comment text (with `///` prefix stripped).
  text: string;
  /// Line number where the doc block starts (1-based).
  startLine: number;
  /// Line number where the doc block ends.
  endLine: number;
}

/// Extracts all `///` doc blocks from source lines.
/// Returns each block with its text and line range.
function extractDocBlocks(
  lines: string[],
): Array<{ block: RawDocBlock; nextLineIdx: number }> {
  const results: Array<{ block: RawDocBlock; nextLineIdx: number }> = [];
  let i = 0;

  while (i < lines.length) {
    const trimmed = lines[i].trimStart();

    if (trimmed.startsWith("///") && !trimmed.startsWith("////")) {
      const startLine = i + 1; // 1-based
      const commentLines: string[] = [];

      while (i < lines.length) {
        const t = lines[i].trimStart();
        if (t.startsWith("///") && !t.startsWith("////")) {
          // Strip `/// ` or `///` prefix
          const content = t.startsWith("/// ")
            ? t.slice(4)
            : t === "///"
              ? ""
              : t.slice(3);
          commentLines.push(content);
          i++;
        } else {
          break;
        }
      }

      results.push({
        block: {
          text: commentLines.join("\n"),
          startLine,
          endLine: startLine + commentLines.length - 1,
        },
        nextLineIdx: i,
      });
    } else {
      i++;
    }
  }

  return results;
}

// ─── Doc Block Parsing ─────────────────────────────────────────

interface ParsedDoc {
  brief: string;
  description?: string;
  sections: Record<string, string>;
  params?: ParamDoc[];
  returns?: string;
  examples?: string[];
  refs?: RefDoc[];
  seeAlso?: string[];
}

/// Parses a raw doc block text into structured metadata.
function parseDocBlock(text: string): ParsedDoc {
  const lines = text.split("\n");

  let brief = "";
  let description = "";
  const sections: Record<string, string> = {};
  const params: ParamDoc[] = [];
  let returns: string | undefined;
  const examples: string[] = [];
  const refs: RefDoc[] = [];
  const seeAlso: string[] = [];

  // State machine
  let currentSection: string | null = null;
  let sectionLines: string[] = [];
  let inBrief = true;
  let briefDone = false;
  let inCodeBlock = false;
  let codeBlockLines: string[] = [];

  function flushSection() {
    if (currentSection && sectionLines.length > 0) {
      const content = sectionLines.join("\n").trim();
      if (currentSection === "Arguments") {
        // Parse bullet list: `* `name` - description`
        for (const line of sectionLines) {
          const m = line.match(
            /^\*\s+`([^`]+)`\s*[-–—]\s*(.+)/,
          );
          if (m) {
            params.push({ name: m[1], description: m[2].trim() });
          }
        }
      } else if (currentSection === "Returns") {
        returns = content;
      } else if (currentSection === "Example") {
        // examples collected via code blocks
        if (content.length > 0 && examples.length === 0) {
          examples.push(content);
        }
      } else {
        sections[currentSection] = content;
      }
    }
    currentSection = null;
    sectionLines = [];
  }

  for (const line of lines) {
    // Track code blocks
    if (line.startsWith("```")) {
      if (inCodeBlock) {
        // End code block
        if (currentSection === "Example") {
          examples.push(codeBlockLines.join("\n"));
        }
        codeBlockLines = [];
        inCodeBlock = false;
        continue;
      } else {
        inCodeBlock = true;
        codeBlockLines = [];
        continue;
      }
    }

    if (inCodeBlock) {
      codeBlockLines.push(line);
      continue;
    }

    // Parse @ref citations — extract ref but keep non-ref text for brief/section
    const refMatch = line.match(
      /@ref\s+(\S+)\s+(\d{4})?\s*"([^"]+)"(?:\s*[—–-]\s*(.+))?/,
    );
    const refSimple = !refMatch
      ? line.match(/@ref\s+(.+?)(?:\s*[—–-]\s*(.+))?$/)
      : null;

    if (refMatch || refSimple) {
      if (refMatch) {
        refs.push({
          author: refMatch[1],
          year: refMatch[2],
          title: refMatch[3],
          note: refMatch[4]?.trim(),
        });
      } else if (refSimple) {
        const parts = refSimple[1].trim().split(/\s+/);
        const yearIdx = parts.findIndex((p) => /^\d{4}$/.test(p));
        if (yearIdx >= 0) {
          refs.push({
            author: parts.slice(0, yearIdx).join(" "),
            year: parts[yearIdx],
            title: parts.slice(yearIdx + 1).join(" "),
            note: refSimple[2]?.trim(),
          });
        } else {
          refs.push({
            author: parts.join(" "),
            title: "",
            note: refSimple[2]?.trim(),
          });
        }
      }

      // Keep any text before the @ref for brief/section accumulation
      const preRef = line.replace(/@ref.*$/, "").trim();
      if (preRef.length > 0) {
        // Don't skip this line — process the pre-ref text below
        // by falling through to brief/section logic with the cleaned text
        const cleanedLine = preRef;
        if (currentSection) {
          sectionLines.push(cleanedLine);
        } else if (inBrief) {
          brief += (brief.length > 0 ? " " : "") + cleanedLine;
        } else if (briefDone) {
          if (cleanedLine !== "" || description.length > 0) {
            description += (description.length > 0 ? "\n" : "") + cleanedLine;
          }
        }
      } else if (currentSection) {
        sectionLines.push(line);
      }
      continue;
    }

    // Parse @see
    const seeMatch = line.match(/@see\s+(.+)/);
    if (seeMatch) {
      seeAlso.push(
        ...seeMatch[1].split(",").map((s) => s.trim()).filter(Boolean),
      );
      continue;
    }

    // Section headers: `# SectionName`
    const headerMatch = line.match(/^#\s+(.+)/);
    if (headerMatch) {
      flushSection();
      currentSection = headerMatch[1].trim();
      inBrief = false;
      briefDone = true;
      continue;
    }

    // Brief / description accumulation
    if (inBrief) {
      if (line === "") {
        if (brief.length > 0) {
          inBrief = false;
          briefDone = true;
        }
      } else {
        brief += (brief.length > 0 ? " " : "") + line;
      }
    } else if (currentSection) {
      sectionLines.push(line);
    } else if (briefDone) {
      // Extended description (between brief and first section)
      if (line !== "" || description.length > 0) {
        description += (description.length > 0 ? "\n" : "") + line;
      }
    }
  }

  flushSection();

  return {
    brief: brief.trim(),
    description: description.trim() || undefined,
    sections,
    params: params.length > 0 ? params : undefined,
    returns,
    examples: examples.length > 0 ? examples : undefined,
    refs: refs.length > 0 ? refs : undefined,
    seeAlso: seeAlso.length > 0 ? seeAlso : undefined,
  };
}

// ─── Declaration Detection ─────────────────────────────────────

interface DeclarationInfo {
  kind: EntityKind;
  name: string;
  type?: string;
  defaultValue?: string;
  line: number;
  bases?: string[];
}

/// Attempts to parse a C++ declaration from a line (and optionally following lines).
function parseDeclaration(
  lines: string[],
  startIdx: number,
): DeclarationInfo | null {
  if (startIdx >= lines.length) return null;

  const line = lines[startIdx].trim();
  const lineNum = startIdx + 1; // 1-based

  // Skip blank lines, preprocessor (except #define), closing braces
  if (line === "" || line === "}" || line === "};") return null;
  if (line.startsWith("#") && !line.startsWith("#define")) return null;
  if (line.startsWith("//")) return null;
  if (line.startsWith("namespace ")) return null;

  // #define UZ_* macro
  const macroMatch = line.match(/^#define\s+(UZ_\w+)/);
  if (macroMatch) {
    return { kind: "macro", name: macroMatch[1], line: lineNum };
  }

  // enum class / enum
  const enumMatch = line.match(
    /^enum\s+(?:class\s+)?(\w+)/,
  );
  if (enumMatch) {
    return { kind: "enum", name: enumMatch[1], line: lineNum };
  }

  // class / struct (with optional inheritance)
  const classMatch = line.match(
    /^(?:template\s*<[^>]*>\s*)?(?:class|struct)\s+(\w+)(?:\s+final)?(?:\s*:\s*(.+?))?\s*(?:\{|$)/,
  );
  if (classMatch) {
    const kind: EntityKind = line.trimStart().startsWith("struct")
      ? "struct"
      : "class";
    const bases = classMatch[2]
      ? classMatch[2]
          .split(",")
          .map((b) => b.replace(/^\s*(?:public|protected|private)\s+/, "").trim())
          .filter(Boolean)
      : undefined;
    return { kind, name: classMatch[1], line: lineNum, bases };
  }

  // typedef / using alias
  const typedefMatch = line.match(
    /^(?:using\s+(\w+)\s*=|typedef\s+.+\s+(\w+)\s*;)/,
  );
  if (typedefMatch) {
    return {
      kind: "typedef",
      name: typedefMatch[1] ?? typedefMatch[2],
      line: lineNum,
    };
  }

  // Field with default value (inside struct/class): `type name = value;`
  const fieldDefaultMatch = line.match(
    /^(?:(?:static|inline|const|constexpr|volatile|mutable)\s+)*(.+?)\s+(\w+)\s*=\s*(.+?)\s*;/,
  );
  if (fieldDefaultMatch && !line.includes("(")) {
    return {
      kind: "field",
      name: fieldDefaultMatch[2],
      type: fieldDefaultMatch[1].trim(),
      defaultValue: fieldDefaultMatch[3].trim(),
      line: lineNum,
    };
  }

  // Field without default: `type name;`
  const fieldMatch = line.match(
    /^(?:(?:static|inline|const|constexpr|volatile|mutable)\s+)*(.+?)\s+(\w+)\s*;/,
  );
  if (fieldMatch && !line.includes("(") && !line.includes("#")) {
    return {
      kind: "field",
      name: fieldMatch[2],
      type: fieldMatch[1].trim(),
      line: lineNum,
    };
  }

  // Function / method: return_type name(params)
  const funcMatch = line.match(
    /^(?:\[\[[\w:]+\]\]\s*)*(?:(?:static|inline|virtual|explicit|constexpr|const)\s+)*(.+?)\s+(\w+)\s*\(([^)]*)\)/,
  );
  if (funcMatch) {
    return {
      kind: "function",
      name: funcMatch[2],
      type: funcMatch[1].trim(),
      line: lineNum,
    };
  }

  return null;
}

// ─── Trailing Comment Parser ───────────────────────────────────

/// Extracts `///<` trailing comments from a line.
function parseTrailingComment(
  line: string,
): string | null {
  const match = line.match(/\/\/\/<?\s*(.+)/);
  if (match && line.indexOf("///<") >= 0) {
    return match[1].trim();
  }
  return null;
}

// ─── Main Parser ───────────────────────────────────────────────

/// Parse a C++ source file and extract all documented entities.
///
/// # Arguments
/// * `source` — full file content as string
/// * `filePath` — relative path from repo root (forward slashes)
///
/// # Returns
/// Array of documented entities found in the file.
export function parseCppFile(
  source: string,
  filePath: string,
): DocumentedEntity[] {
  const lines = source.replace(/\r\n/g, "\n").split("\n");
  const entities: DocumentedEntity[] = [];
  const docBlocks = extractDocBlocks(lines);

  // Process each doc block → look at next non-blank line for declaration
  for (const { block, nextLineIdx } of docBlocks) {
    // Skip blank lines after the doc block
    let declIdx = nextLineIdx;
    while (declIdx < lines.length && lines[declIdx].trim() === "") {
      declIdx++;
    }

    const decl = parseDeclaration(lines, declIdx);
    if (!decl) {
      // File-level doc block (before any declaration, or no matching decl)
      if (block.startLine <= 5) {
        // Likely file-level documentation
        const parsed = parseDocBlock(block.text);
        entities.push({
          id: `${filePath}::__file__`,
          name: filePath.split("/").pop() ?? filePath,
          kind: "file",
          file: filePath,
          line: block.startLine,
          brief: parsed.brief,
          description: parsed.description,
          sections: parsed.sections,
          params: parsed.params,
          returns: parsed.returns,
          examples: parsed.examples,
          refs: parsed.refs,
          seeAlso: parsed.seeAlso,
          tags: [],
          subsystem: "",
          language: "cpp",
          relationships: [],
        });
      }
      continue;
    }

    const parsed = parseDocBlock(block.text);

    // Determine if this is a method (inside a class) or standalone function
    let kind = decl.kind;
    if (kind === "function") {
      // Check if we're inside a class/struct scope by looking backwards
      // for an enclosing class/struct
      for (let j = declIdx - 1; j >= 0; j--) {
        const prevLine = lines[j].trim();
        if (
          prevLine.match(/^(?:class|struct)\s+\w+/) &&
          !prevLine.endsWith(";")
        ) {
          kind = "method";
          break;
        }
        if (prevLine === "};") break;
        if (prevLine.startsWith("namespace ")) break;
      }
    }

    const entity: DocumentedEntity = {
      id: `${filePath}::${decl.name}`,
      name: decl.name,
      kind,
      file: filePath,
      line: decl.line,
      brief: parsed.brief,
      description: parsed.description,
      sections: parsed.sections,
      params: parsed.params,
      returns: parsed.returns,
      examples: parsed.examples,
      refs: parsed.refs,
      seeAlso: parsed.seeAlso,
      tags: [],
      subsystem: "",
      language: "cpp",
      relationships: [],
      type: decl.type,
      defaultValue: decl.defaultValue,
    };

    // Add inheritance relationships
    if (decl.bases) {
      for (const base of decl.bases) {
        entity.relationships.push({
          kind: "inherits",
          target: base,
        });
      }
    }

    entities.push(entity);
  }

  // Second pass: extract fields with trailing `///<` comments
  for (let i = 0; i < lines.length; i++) {
    const line = lines[i];
    const trailing = parseTrailingComment(line);
    if (!trailing) continue;

    // Check if this field already has a doc block above (skip if so)
    const lineNum = i + 1;
    const alreadyDocumented = entities.some(
      (e) => e.line === lineNum && e.kind === "field",
    );
    if (alreadyDocumented) continue;

    // Parse the field declaration from the same line (before the ///<)
    const codePart = line.split("///<")[0];
    const fieldDefaultMatch = codePart.match(
      /(?:(?:static|inline|const|constexpr|volatile|mutable)\s+)*(.+?)\s+(\w+)_?\s*=\s*(.+?)\s*;/,
    );
    const fieldMatch = codePart.match(
      /(?:(?:static|inline|const|constexpr|volatile|mutable)\s+)*(.+?)\s+(\w+)_?\s*;/,
    );

    const match = fieldDefaultMatch ?? fieldMatch;
    if (match) {
      const name = match[2].replace(/_$/, "");
      const existingIdx = entities.findIndex(
        (e) => e.name === name && e.file === filePath,
      );
      if (existingIdx >= 0) continue; // Already documented

      entities.push({
        id: `${filePath}::${name}`,
        name,
        kind: "field",
        file: filePath,
        line: lineNum,
        brief: trailing,
        sections: {},
        tags: [],
        subsystem: "",
        language: "cpp",
        relationships: [],
        type: match[1]?.trim(),
        defaultValue: fieldDefaultMatch ? fieldDefaultMatch[3]?.trim() : undefined,
      });
    }
  }

  // Third pass: scan for undocumented class/struct/enum declarations
  // so they can participate in relationship resolution (inherits, binds_to)
  for (let i = 0; i < lines.length; i++) {
    const line = lines[i].trim();
    const classMatch = line.match(
      /^(?:template\s*<[^>]*>\s*)?(?:class|struct)\s+(\w+)(?:\s+final)?(?:\s*:\s*(.+?))?\s*(?:\{|$)/,
    );
    if (!classMatch) continue;

    const name = classMatch[1];
    const lineNum = i + 1;

    // Skip if already documented
    if (entities.some((e) => e.name === name && e.file === filePath)) continue;
    // Skip forward declarations (line ends with ;)
    if (line.endsWith(";")) continue;

    const kind: EntityKind = line.startsWith("struct") ? "struct" : "class";
    const bases = classMatch[2]
      ? classMatch[2]
          .split(",")
          .map((b) => b.replace(/^\s*(?:public|protected|private)\s+/, "").trim())
          .filter(Boolean)
      : undefined;

    const entity: DocumentedEntity = {
      id: `${filePath}::${name}`,
      name,
      kind,
      file: filePath,
      line: lineNum,
      brief: "",
      sections: {},
      tags: [],
      subsystem: "",
      language: "cpp",
      relationships: [],
    };

    if (bases) {
      for (const base of bases) {
        entity.relationships.push({ kind: "inherits", target: base });
      }
    }

    entities.push(entity);
  }

  // Post-process: merge file-level doc into the first class/struct if the
  // file-level doc has rich content (sections, refs, examples). This handles
  // the common pattern where a header comment documents the main type but is
  // separated from it by #pragma once / #include directives.
  const fileEntity = entities.find((e) => e.kind === "file");
  if (fileEntity) {
    const hasSections = Object.keys(fileEntity.sections).length > 0;
    const hasRefs = (fileEntity.refs?.length ?? 0) > 0;
    const hasExamples = (fileEntity.examples?.length ?? 0) > 0;

    if (hasSections || hasRefs || hasExamples) {
      const mainType = entities.find(
        (e) => (e.kind === "class" || e.kind === "struct" || e.kind === "enum") &&
               e.file === filePath,
      );
      if (mainType) {
        // Merge sections (don't overwrite existing)
        for (const [key, value] of Object.entries(fileEntity.sections)) {
          if (!(key in mainType.sections)) {
            mainType.sections[key] = value;
          }
        }
        // Merge refs
        if (fileEntity.refs?.length) {
          mainType.refs = [...(mainType.refs ?? []), ...fileEntity.refs];
        }
        // Merge examples
        if (fileEntity.examples?.length) {
          mainType.examples = [...(mainType.examples ?? []), ...fileEntity.examples];
        }
        // Merge seeAlso
        if (fileEntity.seeAlso?.length) {
          mainType.seeAlso = [...(mainType.seeAlso ?? []), ...fileEntity.seeAlso];
        }
        // Enrich description if the main type lacks one
        if (!mainType.description && fileEntity.description) {
          mainType.description = fileEntity.description;
        }
      }
    }
  }

  return entities;
}

// ─── Include Extraction ────────────────────────────────────────

/// Extracts `#include "..."` paths from source (not <...> system includes).
export function extractIncludes(source: string): string[] {
  const includes: string[] = [];
  for (const line of source.split("\n")) {
    const match = line.match(/^#include\s+"([^"]+)"/);
    if (match) {
      includes.push(match[1]);
    }
  }
  return includes;
}
