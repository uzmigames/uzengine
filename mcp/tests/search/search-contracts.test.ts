import { describe, expect, it } from 'vitest';
import { Bm25Index } from '../../src/search/bm25.js';
import { buildIdfMap, embed, entityToTokens } from '../../src/search/embeddings.js';
import { SearchEngine } from '../../src/search/search-engine.js';
import type { DocumentedEntity } from '../../tools/types.js';

export function entity(id: string, overrides: Partial<DocumentedEntity> = {}): DocumentedEntity {
  return {
    id,
    name: id,
    kind: 'field',
    file: 'test.h',
    line: 1,
    brief: 'fog density',
    sections: {},
    tags: [],
    subsystem: 'renderer',
    language: 'cpp',
    relationships: [],
    ...overrides,
  };
}

describe('search lifecycle and filters', () => {
  it('returns no documents for empty input and forgets replaced indices', () => {
    const engine = new SearchEngine();
    expect(engine.search('fog')).toEqual([]);
    engine.load([entity('old')]);
    expect(engine.size).toBe(1);
    expect(engine.search('fog')).toHaveLength(1);
    expect(engine.search('the and or')).toEqual([]);
    engine.load([entity('new')]);
    expect(engine.getEntity('old')).toBeUndefined();
    expect(engine.search('fog').map((hit) => hit.id)).toEqual(['new']);
    engine.load([]);
    expect(engine.size).toBe(0);
    expect(engine.getSubsystemMap()).toEqual([]);
    expect(engine.search('fog')).toEqual([]);
  });

  it('applies kind, subsystem, AND tags and limits together', () => {
    const engine = new SearchEngine();
    engine.load([
      entity('match', { tags: ['gpu', 'fog'] }),
      entity('one-tag', { tags: ['gpu'] }),
      entity('wrong-system', { subsystem: 'ecs', tags: ['gpu', 'fog'] }),
      entity('wrong-kind', { kind: 'struct', tags: ['gpu', 'fog'] }),
    ]);
    expect(
      engine
        .search('fog', { kind: 'field', subsystem: 'renderer', tags: ['gpu', 'fog'] })
        .map((hit) => hit.id),
    ).toEqual(['match']);
    expect(engine.search('fog', { tags: [] }, 2)).toHaveLength(2);
    expect(engine.search('fog', {}, 0)).toEqual([]);
    expect(engine.search('fog', { tags: ['absent'] })).toEqual([]);
  });

  it('walks cyclic relationships with depth/kind limits and missing targets', () => {
    const engine = new SearchEngine();
    engine.load([
      entity('a', {
        relationships: [
          { kind: 'uses', target: 'b' },
          { kind: 'contains', target: 'c' },
          { kind: 'uses', target: 'missing' },
        ],
      }),
      entity('b', { relationships: [{ kind: 'uses', target: 'c' }] }),
      entity('c', { relationships: [{ kind: 'uses', target: 'a' }] }),
    ]);
    expect(engine.getRelationships('a', 0)).toEqual([]);
    expect(engine.getRelationships('missing', 3)).toEqual([]);
    expect(engine.getRelationships('a', 1, 'uses').map((e) => e.id)).toEqual(['b']);
    expect(engine.getRelationships('a', 3, 'uses').map((e) => e.id)).toEqual(['b', 'c']);
    expect(engine.getRelationships('a', 1, 'contains').map((e) => e.id)).toEqual(['c']);
  });

  it('merges shared references and filters author and year independently', () => {
    const engine = new SearchEngine();
    const ref = {
      author: 'Bruneton',
      year: '2008',
      title: 'Atmospheric Scattering',
      note: 'reference',
    };
    engine.load([
      entity('a', { refs: [ref] }),
      entity('b', { refs: [ref] }),
      entity('c', { refs: [{ author: 'Karis', title: 'Shading' }] }),
      entity('d'),
    ]);
    expect(engine.getRefs()).toHaveLength(2);
    expect(engine.getRefs('BRUN', '2008')).toEqual([{ ...ref, entityIds: ['a', 'b'] }]);
    expect(engine.getRefs(undefined, '2013')).toEqual([]);
    expect(engine.getRefs('karis')[0].year).toBeUndefined();
  });

  it('orders field data flow from ECS through renderer and shaders', () => {
    const engine = new SearchEngine();
    engine.load([
      entity('other', { name: 'density' }),
      entity('shader', { name: 'density', file: 'runtime/shaders/fog.hlsl', language: 'hlsl' }),
      entity('cb', { name: 'density', kind: 'shader_cb', language: 'hlsl' }),
      entity('renderer', { name: 'density', file: 'runtime/renderer/fog.cpp' }),
      entity('ecs', { name: 'density', file: 'runtime/ecs/fog.h' }),
      entity('component', { name: 'FogComponent' }),
    ]);
    expect(engine.traceDataFlow('density').map((stage) => [stage.entityId, stage.role])).toEqual([
      ['ecs', 'component'],
      ['renderer', 'renderer'],
      ['cb', 'cbuffer'],
      ['shader', 'shader'],
      ['other', 'other'],
    ]);
    expect(engine.traceDataFlow('FogComponent')[0].role).toBe('component');
    expect(engine.traceDataFlow('absent')).toEqual([]);
  });
});

describe('empty and weighted keyword corpora', () => {
  it('handles empty documents, unknown terms, and rebuilding BM25', () => {
    const index = new Bm25Index();
    index.build([]);
    expect(index.search(['fog'], 10)).toEqual([]);
    index.build([[], ['fog', 'fog'], ['fog', 'density']]);
    expect(index.search([], 10)).toEqual([]);
    expect(index.search(['unknown'], 10)).toEqual([]);
    expect(index.search(['density'], 1)[0].doc).toBe(2);
    index.build([['audio']]);
    expect(index.search(['density'], 10)).toEqual([]);
  });

  it('embeds unknown and zero-weight terms as finite zero vectors', () => {
    expect([...embed([], new Map())].every((v) => v === 0)).toBe(true);
    expect([...embed(['unknown'], new Map())].every((v) => v === 0)).toBe(true);
    expect([...embed(['fog'], new Map([['fog', 0]]))].every((v) => v === 0)).toBe(true);
    expect(buildIdfMap([]).size).toBe(0);
    expect(buildIdfMap([['fog', 'fog']]).get('fog')).toBe(buildIdfMap([['fog']]).get('fog'));
  });

  it('retains descriptions, citations and tags while prioritizing entity names', () => {
    const tokens = entityToTokens({
      name: 'FogDensity',
      brief: 'atmosphere',
      description: 'exponential falloff',
      tags: ['volume'],
      refs: [{ author: 'Bruneton', title: 'Scattering' }],
    });
    expect(tokens.filter((t) => t === 'fog')).toHaveLength(3);
    expect(tokens.filter((t) => t === 'atmosphere')).toHaveLength(2);
    expect(tokens).toContain('exponential');
    expect(tokens).toContain('bruneton');
    expect(tokens).toContain('volume');
  });
});
