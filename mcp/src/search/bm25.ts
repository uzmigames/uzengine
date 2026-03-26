/// BM25 keyword search engine.
///
/// In-memory inverted index with BM25 scoring.
/// Field weights are applied by repeating tokens proportionally.

/// BM25 parameters.
const K1 = 1.2;
const B = 0.75;

interface PostingEntry {
  doc: number;
  tf: number;
}

/// In-memory BM25 inverted index.
export class Bm25Index {
  private invertedIndex = new Map<string, PostingEntry[]>();
  private docLengths: number[] = [];
  private avgDocLength = 0;
  private docCount = 0;

  /// Build the index from an array of token arrays (one per document).
  build(documents: string[][]): void {
    this.docCount = documents.length;
    this.docLengths = documents.map((d) => d.length);
    this.avgDocLength =
      this.docLengths.reduce((a, b) => a + b, 0) / (this.docCount || 1);

    this.invertedIndex.clear();

    for (let docIdx = 0; docIdx < documents.length; docIdx++) {
      const doc = documents[docIdx];
      const termFreqs = new Map<string, number>();

      for (const token of doc) {
        termFreqs.set(token, (termFreqs.get(token) ?? 0) + 1);
      }

      for (const [term, tf] of termFreqs) {
        let postings = this.invertedIndex.get(term);
        if (!postings) {
          postings = [];
          this.invertedIndex.set(term, postings);
        }
        postings.push({ doc: docIdx, tf });
      }
    }
  }

  /// Compute IDF for a term.
  private idf(term: string): number {
    const df = this.invertedIndex.get(term)?.length ?? 0;
    return Math.log(1 + (this.docCount - df + 0.5) / (df + 0.5));
  }

  /// Search for documents matching query tokens.
  /// Returns ranked list of (docIndex, score) pairs.
  search(
    queryTokens: string[],
    topK: number,
  ): Array<{ doc: number; score: number }> {
    const scores = new Map<number, number>();

    for (const term of queryTokens) {
      const idf = this.idf(term);
      const postings = this.invertedIndex.get(term);
      if (!postings) continue;

      for (const { doc, tf } of postings) {
        const dl = this.docLengths[doc];
        const tfNorm = (tf * (K1 + 1)) / (tf + K1 * (1 - B + B * dl / this.avgDocLength));
        const termScore = idf * tfNorm;
        scores.set(doc, (scores.get(doc) ?? 0) + termScore);
      }
    }

    return [...scores.entries()]
      .map(([doc, score]) => ({ doc, score }))
      .sort((a, b) => b.score - a.score)
      .slice(0, topK);
  }
}
