/// HLSL/HLSLI shader doc comment parser.
///
/// Extracts documented entities from HLSL shader files: functions, cbuffers,
/// structs, and file-level documentation. Reuses the doc comment parsing
/// logic from the C++ parser for `///` block extraction.

import type {
  DocumentedEntity,
  EntityKind,
  ParamDoc,
  RefDoc,
} from "../types.js";

// ─── Doc Block Extraction (shared pattern with C++ parser) ─────

interface RawDocBlock {
  text: string;
  startLine: number;
  endLine: number;
}

function extractDocBlocks(
  lines: string[],
): Array<{ block: RawDocBlock; nextLineIdx: number }> {
  const results: Array<{ block: RawDocBlock; nextLineIdx: number }> = [];
  let i = 0;

  while (i < lines.length) {
    const trimmed = lines[i].trimStart();
    if (trimmed.startsWith("///") && !trimmed.startsWith("////")) {
      const startLine = i + 1;
      const commentLines: string[] = [];

      while (i < lines.length) {
        const t = lines[i].trimStart();
        if (t.startsWith("///") && !t.startsWith("////")) {
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

// ─── Doc Block Parsing (identical to C++ parser) ───────────────

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
        for (const line of sectionLines) {
          const m = line.match(/^\*\s+`([^`]+)`\s*[-–—]\s*(.+)/);
          if (m) params.push({ name: m[1], description: m[2].trim() });
        }
      } else if (currentSection === "Returns") {
        returns = content;
      } else if (currentSection === "Example") {
        if (content.length > 0 && examples.length === 0) examples.push(content);
      } else {
        sections[currentSection] = content;
      }
    }
    currentSection = null;
    sectionLines = [];
  }

  for (const line of lines) {
    if (line.startsWith("```")) {
      if (inCodeBlock) {
        if (currentSection === "Example") examples.push(codeBlockLines.join("\n"));
        codeBlockLines = [];
        inCodeBlock = false;
        continue;
      } else {
        inCodeBlock = true;
        codeBlockLines = [];
        continue;
      }
    }
    if (inCodeBlock) { codeBlockLines.push(line); continue; }

    const refMatch = line.match(/@ref\s+(\S+)\s+(\d{4})?\s*"([^"]+)"(?:\s*[—–-]\s*(.+))?/);
    const refSimple = !refMatch ? line.match(/@ref\s+(.+?)(?:\s*[—–-]\s*(.+))?$/) : null;
    if (refMatch || refSimple) {
      if (refMatch) {
        refs.push({ author: refMatch[1], year: refMatch[2], title: refMatch[3], note: refMatch[4]?.trim() });
      } else if (refSimple) {
        const parts = refSimple[1].trim().split(/\s+/);
        const yearIdx = parts.findIndex((p) => /^\d{4}$/.test(p));
        if (yearIdx >= 0) {
          refs.push({ author: parts.slice(0, yearIdx).join(" "), year: parts[yearIdx], title: parts.slice(yearIdx + 1).join(" "), note: refSimple[2]?.trim() });
        } else {
          refs.push({ author: parts.join(" "), title: "", note: refSimple[2]?.trim() });
        }
      }
      const preRef = line.replace(/@ref.*$/, "").trim();
      if (preRef.length > 0) {
        if (currentSection) { sectionLines.push(preRef); }
        else if (inBrief) { brief += (brief.length > 0 ? " " : "") + preRef; }
        else if (briefDone && (preRef !== "" || description.length > 0)) {
          description += (description.length > 0 ? "\n" : "") + preRef;
        }
      } else if (currentSection) { sectionLines.push(line); }
      continue;
    }

    const seeMatch = line.match(/@see\s+(.+)/);
    if (seeMatch) {
      seeAlso.push(...seeMatch[1].split(",").map((s) => s.trim()).filter(Boolean));
      continue;
    }

    const headerMatch = line.match(/^#\s+(.+)/);
    if (headerMatch) {
      flushSection();
      currentSection = headerMatch[1].trim();
      inBrief = false;
      briefDone = true;
      continue;
    }

    if (inBrief) {
      if (line === "") {
        if (brief.length > 0) { inBrief = false; briefDone = true; }
      } else {
        brief += (brief.length > 0 ? " " : "") + line;
      }
    } else if (currentSection) {
      sectionLines.push(line);
    } else if (briefDone) {
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

// ─── HLSL Declaration Detection ────────────────────────────────

interface HlslDecl {
  kind: EntityKind;
  name: string;
  type?: string;
  line: number;
  params?: ParamDoc[];
  numthreads?: string;
}

function parseHlslDeclaration(
  lines: string[],
  startIdx: number,
): HlslDecl | null {
  if (startIdx >= lines.length) return null;

  const line = lines[startIdx].trim();
  const lineNum = startIdx + 1;

  if (line === "" || line.startsWith("//") || line.startsWith("#")) return null;

  // cbuffer Name : register(bN) {
  const cbMatch = line.match(/^cbuffer\s+(\w+)/);
  if (cbMatch) {
    return { kind: "shader_cb", name: cbMatch[1], line: lineNum };
  }

  // struct Name {
  const structMatch = line.match(/^struct\s+(\w+)/);
  if (structMatch) {
    return { kind: "struct", name: structMatch[1], line: lineNum };
  }

  // [numthreads(X,Y,Z)] before function
  let numthreads: string | undefined;
  let funcLineIdx = startIdx;
  const ntMatch = line.match(/^\[numthreads\(([^)]+)\)\]/);
  if (ntMatch) {
    numthreads = ntMatch[1];
    funcLineIdx = startIdx + 1;
    if (funcLineIdx >= lines.length) return null;
  }

  const funcLine = lines[funcLineIdx].trim();

  // Function: returnType name(params) optional_semantic {
  const funcMatch = funcLine.match(
    /^(\w[\w:]*(?:\s*<[^>]+>)?)\s+(\w+)\s*\(([^)]*)\)/,
  );
  if (funcMatch) {
    const returnType = funcMatch[1];
    const name = funcMatch[2];

    // Parse parameters
    const paramStr = funcMatch[3].trim();
    const params: ParamDoc[] = [];
    if (paramStr.length > 0) {
      for (const p of paramStr.split(",")) {
        const pt = p.trim();
        // type name : SEMANTIC
        const pm = pt.match(/^(.+?)\s+(\w+)(?:\s*:\s*(\w+))?$/);
        if (pm) {
          params.push({
            name: pm[2],
            description: pm[3] ? `Semantic: ${pm[3]}` : "",
            type: pm[1].trim(),
          });
        }
      }
    }

    return {
      kind: "shader_fn",
      name,
      type: returnType,
      line: lineNum,
      params: params.length > 0 ? params : undefined,
      numthreads,
    };
  }

  return null;
}

// ─── Main Parser ───────────────────────────────────────────────

/// Parse an HLSL/HLSLI shader file and extract documented entities.
///
/// # Arguments
/// * `source` — full file content
/// * `filePath` — relative path from repo root
///
/// # Returns
/// Array of documented entities.
export function parseHlslFile(
  source: string,
  filePath: string,
): DocumentedEntity[] {
  const lines = source.replace(/\r\n/g, "\n").split("\n");
  const entities: DocumentedEntity[] = [];
  const docBlocks = extractDocBlocks(lines);

  for (const { block, nextLineIdx } of docBlocks) {
    let declIdx = nextLineIdx;
    while (declIdx < lines.length && lines[declIdx].trim() === "") {
      declIdx++;
    }

    const decl = parseHlslDeclaration(lines, declIdx);
    if (!decl) {
      // File-level doc if at top of file
      if (block.startLine <= 3) {
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
          language: "hlsl",
          relationships: [],
        });
      }
      continue;
    }

    const parsed = parseDocBlock(block.text);

    const entity: DocumentedEntity = {
      id: `${filePath}::${decl.name}`,
      name: decl.name,
      kind: decl.kind,
      file: filePath,
      line: decl.line,
      brief: parsed.brief,
      description: parsed.description,
      sections: parsed.sections,
      params: parsed.params ?? decl.params,
      returns: parsed.returns,
      examples: parsed.examples,
      refs: parsed.refs,
      seeAlso: parsed.seeAlso,
      tags: [],
      subsystem: "",
      language: "hlsl",
      relationships: [],
      type: decl.type,
    };

    entities.push(entity);
  }

  // Merge file-level doc into first shader function or cbuffer if rich
  const fileEntity = entities.find((e) => e.kind === "file");
  if (fileEntity) {
    const hasRefs = (fileEntity.refs?.length ?? 0) > 0;
    const hasSections = Object.keys(fileEntity.sections).length > 0;
    if (hasRefs || hasSections) {
      const mainEntity = entities.find(
        (e) => e.kind !== "file" && e.file === filePath,
      );
      if (mainEntity) {
        if (fileEntity.refs?.length) {
          mainEntity.refs = [...(mainEntity.refs ?? []), ...fileEntity.refs];
        }
        for (const [key, value] of Object.entries(fileEntity.sections)) {
          if (!(key in mainEntity.sections)) {
            mainEntity.sections[key] = value;
          }
        }
      }
    }
  }

  return entities;
}

/// Extracts `#include "..."` paths from HLSL source.
export function extractHlslIncludes(source: string): string[] {
  const includes: string[] = [];
  for (const line of source.split("\n")) {
    const match = line.match(/^#include\s+"([^"]+)"/);
    if (match) includes.push(match[1]);
  }
  return includes;
}
