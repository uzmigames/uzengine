/// Reciprocal Rank Fusion (RRF) for combining search result lists.
///
/// Combines BM25 and HNSW ranked results into a single ranking.
/// Formula: RRF_score(d) = Σ 1/(k + rank_i(d))

/// RRF constant — dampens high-rank dominance.
const RRF_K = 60;

export interface RankedItem {
  id: number;
  rank: number;
}

export interface FusedResult {
  id: number;
  score: number;
}

/// Combines two ranked lists using Reciprocal Rank Fusion.
///
/// Items appearing in both lists get boosted scores from both contributions.
/// Ranks are 1-based.
export function reciprocalRankFusion(
  listA: RankedItem[],
  listB: RankedItem[],
): FusedResult[] {
  const scores = new Map<number, number>();

  for (const { id, rank } of listA) {
    scores.set(id, (scores.get(id) ?? 0) + 1 / (RRF_K + rank));
  }

  for (const { id, rank } of listB) {
    scores.set(id, (scores.get(id) ?? 0) + 1 / (RRF_K + rank));
  }

  return [...scores.entries()]
    .map(([id, score]) => ({ id, score }))
    .sort((a, b) => b.score - a.score);
}
