/// Webview hardening helpers shared by every UzEngine webview HTML builder.
///
/// Webview pages run with `enableScripts: true` and can post messages that the
/// extension forwards to the native runtime, so any markup injected into the
/// page is a path into the engine. Every page therefore:
///  - interpolates untrusted text only through `escapeHtml`, and
///  - ships a Content-Security-Policy whose scripts and styles are allowed
///    solely by a per-render nonce (no `'unsafe-inline'`).

import { randomBytes } from 'node:crypto';

// ── Nonce ─────────────────────────────────────────────────────────────────────

/// 128 bits of entropy, the minimum the CSP Level 3 specification recommends
/// for a nonce-source ("Content Security Policy Level 3", W3C, §7.4).
const NONCE_BYTES = 16;

/**
 * Returns a fresh, cryptographically random CSP nonce encoded as base64.
 *
 * A new nonce must be generated for every HTML render so an attacker who
 * observes one page cannot reuse its nonce in another.
 */
export function createNonce(): string {
  return randomBytes(NONCE_BYTES).toString('base64');
}

// ── HTML escaping ─────────────────────────────────────────────────────────────

const HTML_ESCAPES: Readonly<Record<string, string>> = {
  '&': '&amp;',
  '<': '&lt;',
  '>': '&gt;',
  '"': '&quot;',
  "'": '&#39;',
};

/**
 * Escapes `value` for safe interpolation into HTML text or a quoted attribute.
 *
 * @param value - Untrusted text (e.g. a name read from a workspace file).
 * @returns The text with `& < > " '` replaced by their character references.
 */
export function escapeHtml(value: string): string {
  return value.replace(/[&<>"']/g, (ch) => HTML_ESCAPES[ch]);
}

// ── Content-Security-Policy ───────────────────────────────────────────────────

/**
 * Builds a least-privilege Content-Security-Policy for an inline webview page
 * that loads no images, media, fonts or network resources.
 *
 * Everything is denied by `default-src 'none'`; inline `<script>`/`<style>`
 * elements run only when they carry the matching nonce. `base-uri` and
 * `form-action` are not covered by `default-src`, so they are denied
 * explicitly to stop injected `<base>`/`<form>` elements from redirecting
 * requests.
 *
 * @param nonce - Per-render nonce from {@link createNonce}; it must also be set
 *   on every `<script>` and `<style>` element of the page.
 * @returns The policy string for a `<meta http-equiv="Content-Security-Policy">`.
 */
export function buildWebviewCsp(nonce: string): string {
  return [
    "default-src 'none'",
    `style-src 'nonce-${nonce}'`,
    `script-src 'nonce-${nonce}'`,
    "base-uri 'none'",
    "form-action 'none'",
  ].join('; ');
}
