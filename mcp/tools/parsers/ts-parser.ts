/// TypeScript doc comment parser.
///
/// Extracts documented entities from TypeScript files by parsing `///`
/// comment blocks. Handles classes, interfaces, functions, type aliases,
/// and exported constants.

import type { DocumentedEntity, EntityKind, RefDoc, ParamDoc } from "../types.js";

// ─── Shared doc block extraction (same pattern as C++/HLSL) ────

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
          const content = t.startsWith("/// ") ? t.slice(4) : t === "///" ? "" : t.slice(3);
          commentLines.push(content);
          i++;
        } else {
          break;
        }
      }
      results.push({
        block: { text: commentLines.join("\n"), startLine, endLine: startLine + commentLines.length - 1 },
        nextLineIdx: i,
      });
    } else {
      i++;
    }
  }
  return results;
}

// ─── Minimal doc block parser ──────────────────────────────────

interface ParsedDoc {
  brief: string;
  description?: string;
  sections: Record<string, string>;
  params?: ParamDoc[];
  returns?: string;
  refs?: RefDoc[];
}

function parseDocBlock(text: string): ParsedDoc {
  const lines = text.split("\n");
  let brief = "";
  let description = "";
  const sections: Record<string, string> = {};
  const params: ParamDoc[] = [];
  let returns: string | undefined;
  const refs: RefDoc[] = [];

  let currentSection: string | null = null;
  let sectionLines: string[] = [];
  let inBrief = true;
  let briefDone = false;

  function flushSection() {
    if (currentSection && sectionLines.length > 0) {
      const content = sectionLines.join("\n").trim();
      if (currentSection === "Arguments") {
        for (const l of sectionLines) {
          const m = l.match(/^\*\s+`([^`]+)`\s*[-–—]\s*(.+)/);
          if (m) params.push({ name: m[1], description: m[2].trim() });
        }
      } else if (currentSection === "Returns") {
        returns = content;
      } else {
        sections[currentSection] = content;
      }
    }
    currentSection = null;
    sectionLines = [];
  }

  for (const line of lines) {
    const refMatch = line.match(/@ref\s+(\S+)\s+(\d{4})?\s*"([^"]+)"(?:\s*[—–-]\s*(.+))?/);
    if (refMatch) {
      refs.push({ author: refMatch[1], year: refMatch[2], title: refMatch[3], note: refMatch[4]?.trim() });
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
    } else if (briefDone && (line !== "" || description.length > 0)) {
      description += (description.length > 0 ? "\n" : "") + line;
    }
  }
  flushSection();

  return {
    brief: brief.trim(),
    description: description.trim() || undefined,
    sections,
    params: params.length > 0 ? params : undefined,
    returns,
    refs: refs.length > 0 ? refs : undefined,
  };
}

// ─── TS Declaration Detection ──────────────────────────────────

function parseTsDeclaration(
  lines: string[],
  startIdx: number,
): { kind: EntityKind; name: string; line: number } | null {
  if (startIdx >= lines.length) return null;
  const line = lines[startIdx].trim();
  const lineNum = startIdx + 1;

  if (line === "" || line.startsWith("//") || line.startsWith("*")) return null;

  // export class / class
  const classMatch = line.match(/^(?:export\s+)?(?:abstract\s+)?class\s+(\w+)/);
  if (classMatch) return { kind: "class", name: classMatch[1], line: lineNum };

  // export interface / interface
  const ifaceMatch = line.match(/^(?:export\s+)?interface\s+(\w+)/);
  if (ifaceMatch) return { kind: "struct", name: ifaceMatch[1], line: lineNum };

  // export type / type
  const typeMatch = line.match(/^(?:export\s+)?type\s+(\w+)/);
  if (typeMatch) return { kind: "typedef", name: typeMatch[1], line: lineNum };

  // export enum / enum
  const enumMatch = line.match(/^(?:export\s+)?(?:const\s+)?enum\s+(\w+)/);
  if (enumMatch) return { kind: "enum", name: enumMatch[1], line: lineNum };

  // export function / function / async function / export async function
  const funcMatch = line.match(/^(?:export\s+)?(?:async\s+)?function\s+(\w+)/);
  if (funcMatch) return { kind: "function", name: funcMatch[1], line: lineNum };

  // export const / const (named constants)
  const constMatch = line.match(/^(?:export\s+)?const\s+(\w+)/);
  if (constMatch) return { kind: "field", name: constMatch[1], line: lineNum };

  return null;
}

// ─── Main Parser ───────────────────────────────────────────────

/// Parse a TypeScript file and extract documented entities.
export function parseTsFile(
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

    const decl = parseTsDeclaration(lines, declIdx);
    if (!decl) {
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
          tags: [],
          subsystem: "",
          language: "typescript",
          relationships: [],
        });
      }
      continue;
    }

    const parsed = parseDocBlock(block.text);
    entities.push({
      id: `${filePath}::${decl.name}`,
      name: decl.name,
      kind: decl.kind,
      file: filePath,
      line: decl.line,
      brief: parsed.brief,
      description: parsed.description,
      sections: parsed.sections,
      params: parsed.params,
      returns: parsed.returns,
      refs: parsed.refs,
      tags: [],
      subsystem: "",
      language: "typescript",
      relationships: [],
    });
  }

  return entities;
}
