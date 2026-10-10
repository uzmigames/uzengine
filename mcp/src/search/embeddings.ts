/// TF-IDF embedding generation using FNV1a hashing.
///
/// Produces 256-dimensional float vectors for search entities.
/// Pure TypeScript — zero external API calls.

import { splitIdentifier, tokenize, removeStopWords } from './tokenizer.js';

/// Embedding dimension.
export const EMBED_DIM = 256;

/// FNV1a 32-bit hash.
export function fnv1a(str: string): number {
  let hash = 0x811c9dc5;
  for (let i = 0; i < str.length; i++) {
    hash ^= str.charCodeAt(i);
    hash = Math.imul(hash, 0x01000193);
  }
  return hash >>> 0;
}

/// Computes a 256-dim TF-IDF embedding from tokens.
///
/// Uses FNV1a hash for bucket assignment and signed hashing to avoid
/// all-positive vector collapse. Result is L2-normalized.
export function embed(tokens: string[], idfMap: Map<string, number>): Float32Array {
  const vec = new Float32Array(EMBED_DIM);
  if (tokens.length === 0) return vec;

  // Count term frequencies
  const tfMap = new Map<string, number>();
  for (const t of tokens) {
    tfMap.set(t, (tfMap.get(t) ?? 0) + 1);
  }

  for (const [term, count] of tfMap) {
    const tf = count / tokens.length;
    const idf = idfMap.get(term) ?? 0;
    const weight = tf * idf;

    const bucket = fnv1a(term) % EMBED_DIM;
    const sign = fnv1a(term + '_sign') % 2 === 0 ? 1 : -1;
    vec[bucket] += sign * weight;
  }

  // L2 normalize
  let norm = 0;
  for (let i = 0; i < EMBED_DIM; i++) norm += vec[i] * vec[i];
  norm = Math.sqrt(norm);
  if (norm > 0) {
    for (let i = 0; i < EMBED_DIM; i++) vec[i] /= norm;
  }

  return vec;
}

/// Computes cosine similarity between two vectors.
export function cosineSimilarity(a: Float32Array, b: Float32Array): number {
  let dot = 0;
  for (let i = 0; i < a.length; i++) dot += a[i] * b[i];
  return dot; // Already L2-normalized, so dot = cosine
}

/// Builds an IDF map from a corpus of token arrays.
///
/// IDF(t) = log(1 + (N - df + 0.5) / (df + 0.5))
export function buildIdfMap(corpus: string[][]): Map<string, number> {
  const N = corpus.length;
  const df = new Map<string, number>();

  for (const doc of corpus) {
    const unique = new Set(doc);
    for (const term of unique) {
      df.set(term, (df.get(term) ?? 0) + 1);
    }
  }

  const idf = new Map<string, number>();
  for (const [term, docFreq] of df) {
    idf.set(term, Math.log(1 + (N - docFreq + 0.5) / (docFreq + 0.5)));
  }

  return idf;
}

/// Tokenizes an entity into a weighted token array for embedding.
///
/// Field weights: name 3×, brief 2×, refs 1.5×, tags 1.5×, description 1×.
export function entityToTokens(entity: {
  name: string;
  brief: string;
  description?: string;
  refs?: Array<{ author: string; title: string }>;
  tags: string[];
}): string[] {
  const tokens: string[] = [];

  // Name (3×)
  const nameTokens = splitIdentifier(entity.name);
  tokens.push(...nameTokens, ...nameTokens, ...nameTokens);

  // Brief (2×)
  const briefTokens = tokenize(entity.brief);
  tokens.push(...briefTokens, ...briefTokens);

  // Description (1×)
  if (entity.description) {
    tokens.push(...tokenize(entity.description));
  }

  // Refs (1.5× — push once full, once half)
  for (const ref of entity.refs ?? []) {
    const refTokens = tokenize(`${ref.author} ${ref.title}`);
    tokens.push(...refTokens);
    tokens.push(...refTokens.slice(0, Math.ceil(refTokens.length / 2)));
  }

  // Tags (1.5×)
  const tagTokens = entity.tags.flatMap((t) => tokenize(t));
  tokens.push(...tagTokens);
  tokens.push(...tagTokens.slice(0, Math.ceil(tagTokens.length / 2)));

  return removeStopWords(tokens);
}
