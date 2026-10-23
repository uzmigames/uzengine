/// Validation of messages posted from UzEngine webviews to the extension host.
///
/// Webview content is untrusted: a compromised or injected page can post any
/// structured-clone value. Each parser below accepts only the message types a
/// panel's own script sends, checks every field's type and range, and returns
/// a fresh object containing exactly the known fields. Anything else yields
/// `undefined` and must be dropped before reaching the runtime.

// ── Limits ────────────────────────────────────────────────────────────────────

/**
 * Largest viewport edge accepted from a `resize` message: the maximum 2D
 * texture dimension in Direct3D 12 (`D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION`),
 * the swap-chain limit of the native renderer.
 */
export const MAX_VIEWPORT_DIMENSION = 16384;

/** Largest absolute pointer position or delta, in CSS pixels. */
export const MAX_POINTER_COORDINATE = 2 * MAX_VIEWPORT_DIMENSION;

/** Largest absolute scroll delta; the webviews send ±1 per wheel event. */
export const MAX_SCROLL_DELTA = 120;

/** Highest `MouseEvent.button` value defined by UI Events (0–4). */
export const MAX_MOUSE_BUTTON = 4;

/** Highest Win32 virtual-key code (`KeyboardEvent.keyCode` uses the same range). */
export const MAX_VIRTUAL_KEY = 0xff;

/** Bit mask of the four modifier flags the viewport sends (Shift|Ctrl|Alt|Meta). */
export const MAX_KEY_MODIFIERS = 0b1111;

/** Longest `KeyboardEvent.code` string accepted (the longest in the spec is 20). */
export const MAX_KEY_CODE_LENGTH = 32;

/** Longest diagnostic text accepted from a webview. */
export const MAX_DIAGNOSTIC_LENGTH = 1024;

// ── Message types ─────────────────────────────────────────────────────────────

/** A webview `resize` request; `0` edges are a legal "collapsed" state. */
export interface ResizeMessage {
  readonly type: 'resize';
  readonly width: number;
  readonly height: number;
}

/** Absolute pointer position inside the canvas. */
export interface MouseMoveMessage {
  readonly type: 'mouseMove';
  readonly x: number;
  readonly y: number;
}

/** DOM mouse button transition. */
export interface MouseButtonMessage {
  readonly type: 'mouseButton';
  readonly button: number;
  readonly pressed: boolean;
}

/** Wheel notch. */
export interface ScrollMessage {
  readonly type: 'scroll';
  readonly delta: number;
}

/** Validated messages accepted from the `.uzscene` custom editor webview. */
export type SceneEditorMessage =
  | { readonly type: 'ready' }
  | MouseMoveMessage
  | MouseButtonMessage
  | { readonly type: 'key'; readonly keycode: number; readonly pressed: boolean }
  | ScrollMessage
  | { readonly type: 'decoderError'; readonly message: string }
  | ResizeMessage;

/** Validated messages accepted from the standalone viewport webview. */
export type ViewportMessage =
  | { readonly type: 'ready' }
  | { readonly type: 'requestKeyframe' }
  | ResizeMessage
  | MouseMoveMessage
  /** Raw pointer-lock deltas (`MouseEvent.movementX/Y`). */
  | { readonly type: 'mouseDelta'; readonly dx: number; readonly dy: number }
  | MouseButtonMessage
  /** `code` is a `KeyboardEvent.code`; `modifiers` defaults to `0` when absent. */
  | {
      readonly type: 'key';
      readonly code: string;
      readonly pressed: boolean;
      readonly modifiers: number;
    }
  | ScrollMessage;

// ── Field predicates ──────────────────────────────────────────────────────────

type UntrustedRecord = Readonly<Record<string, unknown>>;

function isRecord(value: unknown): value is UntrustedRecord {
  return typeof value === 'object' && value !== null;
}

function isIntegerInRange(value: unknown, min: number, max: number): value is number {
  return Number.isInteger(value) && (value as number) >= min && (value as number) <= max;
}

function isFiniteWithin(value: unknown, limit: number): value is number {
  return typeof value === 'number' && Number.isFinite(value) && Math.abs(value) <= limit;
}

function isBoundedString(value: unknown, maxLength: number): value is string {
  return typeof value === 'string' && value.length > 0 && value.length <= maxLength;
}

// ── Shared message parsers ────────────────────────────────────────────────────

function parseResize(raw: UntrustedRecord): ResizeMessage | undefined {
  const { width, height } = raw;
  if (
    !isIntegerInRange(width, 0, MAX_VIEWPORT_DIMENSION) ||
    !isIntegerInRange(height, 0, MAX_VIEWPORT_DIMENSION)
  ) {
    return undefined;
  }
  return { type: 'resize', width, height };
}

function parseMouseMove(raw: UntrustedRecord): MouseMoveMessage | undefined {
  const { x, y } = raw;
  if (!isFiniteWithin(x, MAX_POINTER_COORDINATE) || !isFiniteWithin(y, MAX_POINTER_COORDINATE)) {
    return undefined;
  }
  return { type: 'mouseMove', x, y };
}

function parseMouseButton(raw: UntrustedRecord): MouseButtonMessage | undefined {
  const { button, pressed } = raw;
  if (!isIntegerInRange(button, 0, MAX_MOUSE_BUTTON) || typeof pressed !== 'boolean') {
    return undefined;
  }
  return { type: 'mouseButton', button, pressed };
}

function parseScroll(raw: UntrustedRecord): ScrollMessage | undefined {
  const { delta } = raw;
  if (!isFiniteWithin(delta, MAX_SCROLL_DELTA)) return undefined;
  return { type: 'scroll', delta };
}

// ── Public parsers ────────────────────────────────────────────────────────────

/**
 * Validates a message received from the scene editor webview.
 *
 * @param raw - The value delivered by `webview.onDidReceiveMessage`.
 * @returns A normalized message, or `undefined` if the type is unknown or any
 *   field is missing, of the wrong type, non-finite or out of range.
 */
export function parseSceneEditorMessage(raw: unknown): SceneEditorMessage | undefined {
  if (!isRecord(raw)) return undefined;
  switch (raw.type) {
    case 'ready':
      return { type: 'ready' };
    case 'mouseMove':
      return parseMouseMove(raw);
    case 'mouseButton':
      return parseMouseButton(raw);
    case 'key': {
      const { keycode, pressed } = raw;
      if (!isIntegerInRange(keycode, 0, MAX_VIRTUAL_KEY) || typeof pressed !== 'boolean') {
        return undefined;
      }
      return { type: 'key', keycode, pressed };
    }
    case 'scroll':
      return parseScroll(raw);
    case 'decoderError': {
      const { message } = raw;
      if (!isBoundedString(message, MAX_DIAGNOSTIC_LENGTH)) return undefined;
      return { type: 'decoderError', message };
    }
    case 'resize':
      return parseResize(raw);
    default:
      return undefined;
  }
}

/**
 * Validates a message received from the viewport webview.
 *
 * @param raw - The value delivered by `webview.onDidReceiveMessage`.
 * @returns A normalized message, or `undefined` if the type is unknown or any
 *   field is missing, of the wrong type, non-finite or out of range.
 */
export function parseViewportMessage(raw: unknown): ViewportMessage | undefined {
  if (!isRecord(raw)) return undefined;
  switch (raw.type) {
    case 'ready':
      return { type: 'ready' };
    case 'requestKeyframe':
      return { type: 'requestKeyframe' };
    case 'resize':
      return parseResize(raw);
    case 'mouseMove':
      return parseMouseMove(raw);
    case 'mouseDelta': {
      const { dx, dy } = raw;
      if (
        !isFiniteWithin(dx, MAX_POINTER_COORDINATE) ||
        !isFiniteWithin(dy, MAX_POINTER_COORDINATE)
      ) {
        return undefined;
      }
      return { type: 'mouseDelta', dx, dy };
    }
    case 'mouseButton':
      return parseMouseButton(raw);
    case 'key': {
      const { code, pressed } = raw;
      const modifiers = raw.modifiers ?? 0;
      if (
        !isBoundedString(code, MAX_KEY_CODE_LENGTH) ||
        typeof pressed !== 'boolean' ||
        !isIntegerInRange(modifiers, 0, MAX_KEY_MODIFIERS)
      ) {
        return undefined;
      }
      return { type: 'key', code, pressed, modifiers };
    }
    case 'scroll':
      return parseScroll(raw);
    default:
      return undefined;
  }
}
