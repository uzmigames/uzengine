import { describe, expect, it } from 'vitest';
import {
  MAX_DIAGNOSTIC_LENGTH,
  MAX_KEY_CODE_LENGTH,
  MAX_POINTER_COORDINATE,
  MAX_SCROLL_DELTA,
  MAX_VIEWPORT_DIMENSION,
  parseSceneEditorMessage,
  parseViewportMessage,
} from '../panels/webview-messages';

/** Malformed inputs both panels share (envelope, pointer, button, scroll, resize). */
const sharedMalformed: readonly unknown[] = [
  null,
  undefined,
  'ready',
  42,
  [],
  {},
  { type: 'play' },
  { type: 'loadScene', path: 'C:/Windows/win.ini' },
  { type: 'executeCommand', command: 'workbench.action.terminal.new' },
  { type: 'mouseMove', x: Number.NaN, y: 1 },
  { type: 'mouseMove', x: 1, y: Number.POSITIVE_INFINITY },
  { type: 'mouseMove', x: '1', y: 1 },
  { type: 'mouseMove', x: MAX_POINTER_COORDINATE + 1, y: 0 },
  { type: 'mouseMove', x: 0, y: -MAX_POINTER_COORDINATE - 1 },
  { type: 'mouseButton', button: 5, pressed: true },
  { type: 'mouseButton', button: -1, pressed: true },
  { type: 'mouseButton', button: 1.5, pressed: true },
  { type: 'mouseButton', button: 0, pressed: 'true' },
  { type: 'scroll', delta: Number.NaN },
  { type: 'scroll', delta: MAX_SCROLL_DELTA + 1 },
  { type: 'scroll', delta: '1' },
  { type: 'resize', width: MAX_VIEWPORT_DIMENSION + 1, height: 720 },
  { type: 'resize', width: 1280, height: -1 },
  { type: 'resize', width: 1280.5, height: 720 },
  { type: 'resize', width: 1e9, height: 1e9 },
  { type: 'resize', width: 1280 },
];

describe('scene editor message validation', () => {
  it.each([
    [{ type: 'ready', extra: 'x' }, { type: 'ready' }],
    [
      { type: 'mouseMove', x: -12.5, y: MAX_POINTER_COORDINATE },
      { type: 'mouseMove', x: -12.5, y: MAX_POINTER_COORDINATE },
    ],
    [
      { type: 'mouseButton', button: 2, pressed: false },
      { type: 'mouseButton', button: 2, pressed: false },
    ],
    [
      { type: 'key', keycode: 0xff, pressed: true },
      { type: 'key', keycode: 0xff, pressed: true },
    ],
    [
      { type: 'scroll', delta: -MAX_SCROLL_DELTA },
      { type: 'scroll', delta: -MAX_SCROLL_DELTA },
    ],
    [
      { type: 'decoderError', message: 'x'.repeat(MAX_DIAGNOSTIC_LENGTH) },
      { type: 'decoderError', message: 'x'.repeat(MAX_DIAGNOSTIC_LENGTH) },
    ],
    [
      { type: 'resize', width: 0, height: MAX_VIEWPORT_DIMENSION },
      { type: 'resize', width: 0, height: MAX_VIEWPORT_DIMENSION },
    ],
  ])('accepts %j and keeps only known fields', (raw, expected) => {
    expect(parseSceneEditorMessage(raw)).toStrictEqual(expected);
  });

  it.each([
    ...sharedMalformed,
    { type: 'requestKeyframe' },
    { type: 'mouseDelta', dx: 1, dy: 1 },
    { type: 'key', keycode: 0x100, pressed: true },
    { type: 'key', keycode: 0x10000 + 0x57, pressed: true },
    { type: 'key', keycode: 65, pressed: 1 },
    { type: 'key', code: 'KeyA', pressed: true },
    { type: 'decoderError', message: '' },
    { type: 'decoderError', message: 'x'.repeat(MAX_DIAGNOSTIC_LENGTH + 1) },
    { type: 'decoderError', message: { toString: () => 'x' } },
  ])('rejects %j', (raw) => {
    expect(parseSceneEditorMessage(raw)).toBeUndefined();
  });
});

describe('viewport message validation', () => {
  it.each([
    [{ type: 'ready' }, { type: 'ready' }],
    [{ type: 'requestKeyframe', force: true }, { type: 'requestKeyframe' }],
    [
      { type: 'resize', width: 640, height: 480 },
      { type: 'resize', width: 640, height: 480 },
    ],
    [
      { type: 'mouseMove', x: 3, y: 4 },
      { type: 'mouseMove', x: 3, y: 4 },
    ],
    [
      { type: 'mouseDelta', dx: -7, dy: 0.5 },
      { type: 'mouseDelta', dx: -7, dy: 0.5 },
    ],
    [
      { type: 'mouseButton', button: 4, pressed: true },
      { type: 'mouseButton', button: 4, pressed: true },
    ],
    [
      { type: 'key', code: 'KeyW', pressed: true, modifiers: 0b1111 },
      { type: 'key', code: 'KeyW', pressed: true, modifiers: 0b1111 },
    ],
    [
      { type: 'key', code: 'Space', pressed: false },
      { type: 'key', code: 'Space', pressed: false, modifiers: 0 },
    ],
    [
      { type: 'scroll', delta: 1 },
      { type: 'scroll', delta: 1 },
    ],
  ])('accepts %j and keeps only known fields', (raw, expected) => {
    expect(parseViewportMessage(raw)).toStrictEqual(expected);
  });

  it.each([
    ...sharedMalformed,
    { type: 'decoderError', message: 'x' },
    { type: 'mouseDelta', dx: Number.NaN, dy: 0 },
    { type: 'mouseDelta', dx: 0, dy: MAX_POINTER_COORDINATE + 1 },
    { type: 'key', code: '', pressed: true },
    { type: 'key', code: 'K'.repeat(MAX_KEY_CODE_LENGTH + 1), pressed: true },
    { type: 'key', code: 87, pressed: true },
    { type: 'key', code: 'KeyW', pressed: null },
    { type: 'key', code: 'KeyW', pressed: true, modifiers: 16 },
    { type: 'key', code: 'KeyW', pressed: true, modifiers: -1 },
    { type: 'key', code: 'KeyW', pressed: true, modifiers: 1.5 },
    { type: 'key', code: 'KeyW', pressed: true, modifiers: '2' },
  ])('rejects %j', (raw) => {
    expect(parseViewportMessage(raw)).toBeUndefined();
  });
});
