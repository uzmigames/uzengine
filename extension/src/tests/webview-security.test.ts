import { describe, expect, it } from 'vitest';
import { buildWebviewCsp, createNonce, escapeHtml } from '../panels/webview-security';

describe('webview hardening helpers', () => {
  it('escapes every HTML-significant character exactly once and keeps other text intact', () => {
    expect(escapeHtml(`</title><script>alert("x")</script>&'`)).toBe(
      '&lt;/title&gt;&lt;script&gt;alert(&quot;x&quot;)&lt;/script&gt;&amp;&#39;',
    );
    expect(escapeHtml('&lt;')).toBe('&amp;lt;');
    expect(escapeHtml('Level 1 — Forest')).toBe('Level 1 — Forest');
    expect(escapeHtml('')).toBe('');
  });

  it('creates distinct 128-bit base64 nonces', () => {
    const nonces = new Set(Array.from({ length: 64 }, () => createNonce()));
    expect(nonces.size).toBe(64);
    for (const nonce of nonces) {
      expect(nonce).toMatch(/^[A-Za-z0-9+/]{22}==$/);
      expect(Buffer.from(nonce, 'base64')).toHaveLength(16);
    }
  });

  it('denies everything except nonce-tagged inline scripts and styles', () => {
    expect(buildWebviewCsp('abc123==')).toBe(
      "default-src 'none'; style-src 'nonce-abc123=='; script-src 'nonce-abc123=='; " +
        "base-uri 'none'; form-action 'none'",
    );
  });
});
