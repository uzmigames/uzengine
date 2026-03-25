import { describe, it, expect } from "vitest";
import { parseTsFile } from "../../tools/parsers/ts-parser.js";
import { readFileSync } from "node:fs";
import { resolve } from "node:path";

describe("parseTsFile — synthetic", () => {
  it("parses exported function", () => {
    const source = `/// Tokenizes free-form text.
///
/// # Arguments
/// * \`text\` - Raw text to tokenize.
///
/// # Returns
/// Array of lowercase tokens.
export function tokenize(text: string): string[] {
  return text.toLowerCase().split(/\\s+/);
}`;
    const entities = parseTsFile(source, "src/search/tokenizer.ts");

    const fn = entities.find((e) => e.name === "tokenize");
    expect(fn).toBeDefined();
    expect(fn!.kind).toBe("function");
    expect(fn!.params).toHaveLength(1);
    expect(fn!.params![0].name).toBe("text");
    expect(fn!.returns).toContain("Array of lowercase");
  });

  it("parses interface as struct kind", () => {
    const source = `/// A search result from the doc index.
export interface SearchResult {
  id: string;
  name: string;
}`;
    const entities = parseTsFile(source, "src/types.ts");

    const iface = entities.find((e) => e.name === "SearchResult");
    expect(iface).toBeDefined();
    expect(iface!.kind).toBe("struct");
  });

  it("parses class and const", () => {
    const source = `/// MCP server instance.
const server = new McpServer({});

/// Logger for structured output.
export class AppLogger {}`;
    const entities = parseTsFile(source, "src/server.ts");

    expect(entities.find((e) => e.name === "server")).toBeDefined();
    expect(entities.find((e) => e.name === "AppLogger")).toBeDefined();
  });

  it("parses file-level doc", () => {
    const source = `/// UzEngine MCP server.
///
/// Implements the MCP interface for Claude integration.

import { McpServer } from "@modelcontextprotocol/sdk";`;
    const entities = parseTsFile(source, "mcp/src/server.ts");

    const fileDoc = entities.find((e) => e.kind === "file");
    expect(fileDoc).toBeDefined();
    expect(fileDoc!.brief).toContain("UzEngine MCP");
  });
});

describe("parseTsFile — real files", () => {
  it("parses mcp/src/server.ts", () => {
    const path = resolve("../src/server.ts");
    let source: string;
    try {
      source = readFileSync(path, "utf-8");
    } catch {
      return;
    }

    const entities = parseTsFile(source, "mcp/src/server.ts");
    expect(entities.length).toBeGreaterThan(0);

    const fileDoc = entities.find((e) => e.kind === "file");
    expect(fileDoc).toBeDefined();
  });
});
