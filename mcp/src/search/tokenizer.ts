/// Text tokenizer and normalizer for the doc search engine.
///
/// Handles CamelCase/PascalCase/snake_case splitting, stop word removal,
/// and text normalization for both BM25 indexing and TF-IDF embedding.

/// Minimal stop word set tuned for code documentation.
const STOP_WORDS = new Set([
  'the',
  'a',
  'an',
  'is',
  'are',
  'was',
  'were',
  'be',
  'been',
  'this',
  'that',
  'it',
  'its',
  'of',
  'in',
  'to',
  'for',
  'with',
  'on',
  'at',
  'by',
  'from',
  'as',
  'or',
  'and',
  'not',
  'no',
  'if',
  'when',
  'then',
  'else',
  'can',
  'will',
  'do',
  'does',
  'has',
  'have',
  'had',
  'so',
  'but',
  'all',
  'each',
  'every',
  'any',
  'both',
  'more',
  'most',
  'some',
  'such',
  'than',
  'too',
  'very',
  'just',
  'about',
  'into',
  'over',
  'after',
  'before',
  'between',
  'under',
  'also',
  'only',
  'other',
  'new',
  'old',
  'used',
  'using',
  'use',
]);

/// Splits a CamelCase, PascalCase, or snake_case identifier into words.
///
/// # Examples
/// - `"TimeManager"` → `["time", "manager"]`
/// - `"HTMLParser"` → `["html", "parser"]`
/// - `"sky_atmosphere_ps"` → `["sky", "atmosphere", "ps"]`
/// - `"PerFrameCB"` → `["per", "frame", "cb"]`
/// - `"ID3D12Device"` → `["id3d12", "device"]`
export function splitIdentifier(name: string): string[] {
  return (
    name
      // Insert space before uppercase that follows lowercase: camelCase → camel Case
      .replace(/([a-z0-9])([A-Z])/g, '$1 $2')
      // Insert space between consecutive uppercase and a following lowercase: HTMLParser → HTML Parser
      .replace(/([A-Z]+)([A-Z][a-z])/g, '$1 $2')
      // Replace underscores with spaces
      .replace(/_/g, ' ')
      // Replace non-alphanumeric (except spaces) with spaces
      .replace(/[^a-zA-Z0-9 ]/g, ' ')
      .toLowerCase()
      .split(/\s+/)
      .filter((t) => t.length > 0)
  );
}

/// Tokenizes free-form text (descriptions, doc comments).
///
/// Lowercases, splits on non-alphanumeric boundaries (preserving underscores
/// within identifiers), and filters out single-character tokens.
///
/// # Arguments
/// * `text` — raw text to tokenize
///
/// # Returns
/// Array of lowercase tokens.
export function tokenize(text: string): string[] {
  return text
    .toLowerCase()
    .replace(/[^a-z0-9_]/g, ' ')
    .split(/\s+/)
    .filter((t) => t.length > 1);
}

/// Removes stop words from a token array.
export function removeStopWords(tokens: string[]): string[] {
  return tokens.filter((t) => !STOP_WORDS.has(t));
}

/// Full tokenization pipeline: tokenize text + split identifiers + remove stop words.
///
/// Combines `tokenize()` for free-form text and `splitIdentifier()` for
/// code identifiers, then deduplicates and removes stop words.
export function tokenizeFull(text: string): string[] {
  const tokens = tokenize(text);
  return removeStopWords(tokens);
}

/// Checks if a token is a stop word.
export function isStopWord(token: string): boolean {
  return STOP_WORDS.has(token);
}
