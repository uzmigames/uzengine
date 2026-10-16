import { describe, expect, it } from 'vitest';
import { FrameGate } from '../frame-gate.js';

describe('FrameGate', () => {
  it('holds fresh deltas until the first key, then delivers deltas', () => {
    const gate = new FrameGate();
    expect(gate.awaitingKeyframe).toBe(true);
    expect(gate.shouldDeliver(false)).toBe(false);
    expect(gate.shouldDeliver(true)).toBe(true);
    expect(gate.awaitingKeyframe).toBe(false);
    expect(gate.shouldDeliver(false)).toBe(true);
  });

  it('holds deltas after repeated drops until the next key', () => {
    const gate = new FrameGate();
    gate.shouldDeliver(true);
    gate.onDropped();
    expect(gate.shouldDeliver(false)).toBe(false);
    gate.onDropped();
    expect(gate.shouldDeliver(false)).toBe(false);
    expect(gate.shouldDeliver(true)).toBe(true);
    expect(gate.shouldDeliver(false)).toBe(true);
  });

  it('rearms an existing stream', () => {
    const gate = new FrameGate();
    gate.shouldDeliver(true);
    gate.arm();
    expect(gate.awaitingKeyframe).toBe(true);
    expect(gate.shouldDeliver(false)).toBe(false);
    expect(gate.shouldDeliver(true)).toBe(true);
  });
});
