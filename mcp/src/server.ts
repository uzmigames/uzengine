/// UzEngine MCP (Model Context Protocol) server.
///
/// Implements the MCP server interface for Claude integration, enabling
/// LLM access to UzEngine runtime capabilities via stdio transport.
/// Includes hybrid BM25 + HNSW documentation search tools.

import { McpServer } from '@modelcontextprotocol/sdk/server/mcp.js';
import { StdioServerTransport } from '@modelcontextprotocol/sdk/server/stdio.js';
import { z } from 'zod';
import { readFileSync, existsSync } from 'node:fs';
import { resolve } from 'node:path';
import { mcpLog } from './logger.js';
import { SearchEngine } from './search/search-engine.js';
import type { DocsDatabase, EntityKind } from '../tools/types.js';

/// Creates the MCP server instance for UzEngine.
const server = new McpServer({
  name: 'uzengine-mcp',
  version: '0.3.0',
});

/// Global search engine instance.
const searchEngine = new SearchEngine();

/// All valid entity kinds for filtering.
const ENTITY_KINDS: EntityKind[] = [
  'file', 'class', 'struct', 'enum', 'enum_value', 'function',
  'method', 'field', 'macro', 'shader_fn', 'shader_cb', 'typedef',
];

// ─── Tool Registration ─────────────────────────────────────────

/// search_docs — Primary hybrid BM25 + HNSW search.
server.tool(
  'search_docs',
  'Search UzEngine codebase documentation using hybrid BM25 + HNSW semantic search',
  {
    query: z.string().describe('Search term or natural language query'),
    kind: z.enum(ENTITY_KINDS as [string, ...string[]]).optional().describe('Filter by entity kind'),
    subsystem: z.string().optional().describe('Filter by subsystem name'),
    tags: z.array(z.string()).optional().describe('Filter by tags (AND logic)'),
    limit: z.number().default(10).describe('Max results to return'),
  },
  async ({ query, kind, subsystem, tags, limit }) => {
    const filters = {
      kind: kind as EntityKind | undefined,
      subsystem,
      tags,
    };
    const results = searchEngine.search(query, filters, limit);
    return {
      content: [{
        type: 'text' as const,
        text: JSON.stringify({ results, total: results.length, query }, null, 2),
      }],
    };
  },
);

/// get_entity — Fetch full entity by ID.
server.tool(
  'get_entity',
  'Get complete documentation for a specific code entity by ID',
  {
    id: z.string().describe('Entity ID (file_path::entity_name)'),
  },
  async ({ id }) => {
    const entity = searchEngine.getEntity(id);
    return {
      content: [{
        type: 'text' as const,
        text: entity
          ? JSON.stringify(entity, null, 2)
          : JSON.stringify({ error: 'Entity not found', id }),
      }],
    };
  },
);

/// get_relationships — Graph traversal from entity.
server.tool(
  'get_relationships',
  'Get entities related to a given entity via relationships (inheritance, binding, etc.)',
  {
    id: z.string().describe('Entity ID to traverse from'),
    depth: z.number().default(1).describe('Traversal depth (1-3)'),
    kind: z.string().optional().describe('Filter by relationship kind'),
  },
  async ({ id, depth, kind }) => {
    const related = searchEngine.getRelationships(id, depth, kind);
    return {
      content: [{
        type: 'text' as const,
        text: JSON.stringify({
          source: id,
          depth,
          related: related.map((e) => ({
            id: e.id,
            name: e.name,
            kind: e.kind,
            file: e.file,
            brief: e.brief,
          })),
        }, null, 2),
      }],
    };
  },
);

/// get_data_flow — Trace a field from component → CB → shader.
server.tool(
  'get_data_flow',
  'Trace a field name through the component → renderer → cbuffer → shader pipeline',
  {
    field: z.string().describe('Field name to trace (e.g. "fogDensity")'),
  },
  async ({ field }) => {
    const stages = searchEngine.traceDataFlow(field);
    return {
      content: [{
        type: 'text' as const,
        text: JSON.stringify({ field, stages }, null, 2),
      }],
    };
  },
);

/// get_subsystem_map — Overview of all subsystems.
server.tool(
  'get_subsystem_map',
  'List all code subsystems with entity counts',
  {},
  async () => {
    const map = searchEngine.getSubsystemMap();
    return {
      content: [{
        type: 'text' as const,
        text: JSON.stringify(map, null, 2),
      }],
    };
  },
);

/// get_refs — Academic references index.
server.tool(
  'get_refs',
  'List academic paper references cited in the codebase',
  {
    author: z.string().optional().describe('Filter by author name'),
    year: z.string().optional().describe('Filter by year'),
  },
  async ({ author, year }) => {
    const refs = searchEngine.getRefs(author, year);
    return {
      content: [{
        type: 'text' as const,
        text: JSON.stringify(refs, null, 2),
      }],
    };
  },
);

// ─── Index Loading ─────────────────────────────────────────────

/// Loads the doc search index from docs.json at startup.
function loadSearchIndex(): boolean {
  const docsPath = resolve(import.meta.dirname ?? '.', '../../data/docs.json');

  if (!existsSync(docsPath)) {
    mcpLog.warn('docs.json not found — run "npm run generate-docs" first. Search tools will return empty results.');
    return false;
  }

  try {
    const startTime = performance.now();
    const raw = readFileSync(docsPath, 'utf-8');
    const db: DocsDatabase = JSON.parse(raw);

    searchEngine.load(db.entities);

    const elapsed = (performance.now() - startTime).toFixed(0);
    mcpLog.info(`Search index loaded: ${db.entities.length} entities in ${elapsed}ms`);
    return true;
  } catch (err) {
    mcpLog.error({ err }, 'Failed to load search index');
    return false;
  }
}

// ─── Main ──────────────────────────────────────────────────────

/// Initializes and runs the MCP server.
async function main(): Promise<void> {
  mcpLog.info('UzEngine MCP server starting');

  // Load search index
  loadSearchIndex();

  const transport = new StdioServerTransport();
  await server.connect(transport);
  mcpLog.info('UzEngine MCP server connected (6 doc search tools registered)');
}

/// Entry point: starts the MCP server.
main().catch((err) => {
  mcpLog.fatal({ err }, 'MCP server fatal error');
  process.exit(1);
});
