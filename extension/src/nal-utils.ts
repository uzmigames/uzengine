/** Inspects the first VCL NAL in an Annex-B access unit, skipping AUD/SPS/PPS. */
export function isAnnexBKeyframe(data: Uint8Array): boolean {
  for (let i = 0; i + 3 < data.length; i++) {
    if (data[i] !== 0 || data[i + 1] !== 0) continue;
    let header: number;
    if (data[i + 2] === 1) {
      header = i + 3;
    } else if (data[i + 2] === 0 && data[i + 3] === 1 && i + 4 < data.length) {
      header = i + 4;
    } else {
      continue;
    }
    const type = data[header] & 0x1f;
    if (type >= 1 && type <= 5) return type === 5;
    i = header;
  }
  return false;
}
