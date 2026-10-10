import { afterEach, describe, expect, it } from 'vitest';
import { mkdtempSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { Client } from '@modelcontextprotocol/sdk/client/index.js';
import { InMemoryTransport } from '@modelcontextprotocol/sdk/inMemory.js';
import type { McpServer } from '@modelcontextprotocol/sdk/server/mcp.js';
import type { DocumentedEntity } from '../tools/types.js';
import { SearchEngine } from '../src/search/search-engine.js';
import { createServer, loadSearchIndex, startServer } from '../src/server.js';

const fog: DocumentedEntity = {
  id: 'fog.h::density',
  name: 'density',
  kind: 'field',
  file: 'runtime/ecs/fog.h',
  line: 17,
  brief: 'Height fog density',
  sections: {},
  tags: ['fog'],
  subsystem: 'environment',
  language: 'cpp',
  relationships: [{ kind: 'uses', target: 'fog.hlsl::density' }],
  refs: [{ author: 'Bruneton', year: '2008', title: 'Atmospheric Scattering' }],
};
const shader: DocumentedEntity = {
  ...fog,
  id: 'fog.hlsl::density',
  file: 'runtime/shaders/fog.hlsl',
  language: 'hlsl',
  relationships: [],
  refs: [],
};
const clients: Client[] = [];
const servers: McpServer[] = [];
const directories: string[] = [];

afterEach(async () => {
  await Promise.all(clients.splice(0).map((client) => client.close()));
  await Promise.all(servers.splice(0).map((server) => server.close()));
  directories.splice(0).forEach((dir) => rmSync(dir, { recursive: true, force: true }));
});

function fixture(content: string): string {
  const dir = mkdtempSync(join(tmpdir(), 'uz-mcp-test-'));
  directories.push(dir);
  const path = join(dir, 'docs.json');
  writeFileSync(path, content);
  return path;
}

async function connect(entities = [fog, shader]): Promise<Client> {
  const engine = new SearchEngine();
  engine.load(entities);
  const server = createServer(engine);
  servers.push(server);
  const [clientTransport, serverTransport] = InMemoryTransport.createLinkedPair();
  await server.connect(serverTransport);
  const client = new Client({ name: 'validation', version: '1.0.0' });
  clients.push(client);
  await client.connect(clientTransport);
  return client;
}

async function call(
  client: Client,
  name: string,
  args: Record<string, unknown> = {},
): Promise<unknown> {
  const result = await client.callTool({ name, arguments: args });
  expect(result.isError).not.toBe(true);
  const content = result.content as Array<{ type: string; text: string }>;
  expect(content).toHaveLength(1);
  expect(content[0].type).toBe('text');
  return JSON.parse(content[0].text);
}

describe('MCP protocol against a real in-memory transport', () => {
  it('advertises all six tools and validates input schemas', async () => {
    const client = await connect();
    const tools = await client.listTools();
    expect(tools.tools.map((tool) => tool.name).sort()).toEqual([
      'get_data_flow',
      'get_entity',
      'get_refs',
      'get_relationships',
      'get_subsystem_map',
      'search_docs',
    ]);
    expect((await client.callTool({ name: 'search_docs', arguments: { query: 42 } })).isError).toBe(
      true,
    );
    expect(
      (await client.callTool({ name: 'search_docs', arguments: { query: 'fog', kind: 'invalid' } }))
        .isError,
    ).toBe(true);
  });

  it('serves search, entities and explicit missing-entity results', async () => {
    const client = await connect();
    expect(
      await call(client, 'search_docs', {
        query: 'fog',
        kind: 'field',
        subsystem: 'environment',
        tags: ['fog'],
        limit: 1,
      }),
    ).toMatchObject({
      query: 'fog',
      total: 1,
      results: [expect.objectContaining({ name: 'density' })],
    });
    expect(await call(client, 'search_docs', { query: 'fog' })).toMatchObject({ total: 2 });
    expect(await call(client, 'get_entity', { id: fog.id })).toEqual(fog);
    expect(await call(client, 'get_entity', { id: 'missing' })).toEqual({
      error: 'Entity not found',
      id: 'missing',
    });
  });

  it('serves relationships, pipeline order, subsystems and paper references', async () => {
    const client = await connect();
    expect(await call(client, 'get_relationships', { id: fog.id })).toMatchObject({
      source: fog.id,
      depth: 1,
      related: [{ id: shader.id, name: shader.name, kind: 'field' }],
    });
    expect(
      await call(client, 'get_relationships', { id: fog.id, depth: 2, kind: 'contains' }),
    ).toMatchObject({ depth: 2, related: [] });
    expect(await call(client, 'get_data_flow', { field: 'density' })).toMatchObject({
      field: 'density',
      stages: [{ role: 'component' }, { role: 'shader' }],
    });
    expect(await call(client, 'get_subsystem_map')).toEqual([
      { subsystem: 'environment', entityCount: 2 },
    ]);
    expect(await call(client, 'get_refs', { author: 'bruneton', year: '2008' })).toEqual([
      { ...fog.refs![0], entityIds: [fog.id] },
    ]);
    expect(await call(client, 'get_refs', { year: '2020' })).toEqual([]);
  });

  it('keeps server instances independent', async () => {
    const populated = await connect();
    const empty = await connect([]);
    expect(await call(empty, 'search_docs', { query: 'fog' })).toMatchObject({
      total: 0,
      results: [],
    });
    expect(await call(populated, 'search_docs', { query: 'fog' })).toMatchObject({ total: 2 });
  });
});

describe('documentation startup', () => {
  it('loads the exact supplied fixture and starts the real protocol transport', async () => {
    const docsPath = fixture(JSON.stringify({ version: 1, entities: [fog, shader] }));
    const [clientTransport, serverTransport] = InMemoryTransport.createLinkedPair();
    servers.push(await startServer(docsPath, serverTransport));
    const client = new Client({ name: 'startup-test', version: '1.0.0' });
    clients.push(client);
    await client.connect(clientTransport);
    expect(await call(client, 'get_entity', { id: fog.id })).toEqual(fog);
  });

  it('reports missing and corrupt databases as unsuccessful loads', () => {
    const engine = new SearchEngine();
    engine.load([fog]);
    expect(engine.search('fog')).toHaveLength(1);
    const docsPath = fixture('{broken');
    expect(loadSearchIndex(engine, join(docsPath, 'missing'))).toBe(false);
    expect(loadSearchIndex(engine, docsPath)).toBe(false);
    expect(loadSearchIndex(engine, fixture('{"entities":null}'))).toBe(false);
    expect(loadSearchIndex(engine, fixture('{"entities":[{"name":"broken"}]}'))).toBe(false);
    expect(engine.size).toBe(1);
    expect(engine.getEntity(fog.id)).toEqual(fog);
    expect(engine.search('fog').map((result) => result.id)).toEqual([fog.id]);
  });

  it('propagates transport startup failure', async () => {
    const docsPath = fixture(JSON.stringify({ entities: [] }));
    const [, transport] = InMemoryTransport.createLinkedPair();
    transport.start = async () => {
      throw new Error('transport unavailable');
    };
    await expect(startServer(docsPath, transport)).rejects.toThrow('transport unavailable');
  });
});
