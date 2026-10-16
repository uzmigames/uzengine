import { describe, expect, it } from 'vitest';
import { isAnnexBKeyframe } from '../nal-utils.js';

describe('isAnnexBKeyframe', () => {
  it.each([
    ['IDR after SPS/PPS', [0, 0, 0, 1, 0x67, 0, 0, 1, 0x68, 0, 0, 1, 0x65], true],
    ['AUD then IDR', [0, 0, 1, 0x69, 0, 0, 0, 1, 0x65], true],
    ['three-byte start code', [0, 0, 1, 0x65], true],
    ['four-byte start code', [0, 0, 0, 1, 0x65], true],
    ['non-IDR slice', [0, 0, 1, 0x61], false],
    ['first VCL is non-IDR', [0, 0, 1, 0x61, 0, 0, 1, 0x65], false],
    ['SPS/PPS only', [0, 0, 1, 0x67, 0, 0, 1, 0x68], false],
    ['empty', [], false],
    ['truncated three-byte code', [0, 0, 1], false],
    ['truncated four-byte code', [0, 0, 0, 1], false],
    ['missing start code', [0x65], false],
  ] as const)('%s', (_name, data, expected) => {
    expect(isAnnexBKeyframe(Uint8Array.from(data))).toBe(expected);
  });
});
