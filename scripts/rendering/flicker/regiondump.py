"""Reader for the region-dump files written by `uzengine.exe --region-dump` (format version 2).

The engine writes one file per rendered frame holding per-pixel linear-HDR records for one or
more screen rectangles. Every rectangle carries GPU-written frame stamps; a file whose stamps
disagree with the frame the CPU read back is marked invalid and MUST NOT be analysed as data.
See `runtime/shaders/runtime/common/region_dump.hlsli` for the record layout and
`SceneRenderer::SetRegionDump` for the capture path.

File layout (little-endian):
    header  64 B  magic 'UZR2', version, frame, rectCount, fields, recordsPerPixel, flags,
                  renderW, renderH, outputW, outputH, jitterIndex, jitterX, jitterY, exposure, pad
    camera 144 B  version 3 only: unjittered view-projection, the previous frame's, camera position
    rects   32 B x rectCount  name[16], x, y, w, h
    stamps  16 B x rectCount  lightingStamp, taaStamp, stampFlags, baseOffset
    data          float4 records: per rect, w*h*recordsPerPixel pixel records + 4 footer records

Example:
    from regiondump import load_sequence
    frames = load_sequence(run_dir, 300, 336)          # raises on gaps or invalid stamps
    win = frames[0].rect('window')
    rgb = win.channel('taa_output')[..., :3]           # (h, w, 3) linear HDR
"""
from __future__ import annotations

import glob
import os
import re
import struct

import numpy as np

MAGIC_V2 = 0x32525A55  # 'UZR2'
MAGIC_V1 = 0x44525A55  # 'UZRD' — the first single-rect format (no frame stamps)

FIELD_LIGHTING = 1
FIELD_TAA = 2
FIELD_OUTPUT = 4
FIELD_TAA_EXTRA = 8
FIELD_LIGHTING_EXTRA = 16
FIELD_GATHER = 32

FLAG_TAA_ACTIVE = 1 << 0
FLAG_LIGHTING_MISMATCH = 1 << 1
FLAG_TAA_MISMATCH = 1 << 2
FLAG_LIGHTING_MISSING = 1 << 3
FLAG_TAA_MISSING = 1 << 4
FLAG_RESOLUTION_SPLIT = 1 << 5
FLAG_GATHER_MISMATCH = 1 << 6
FLAG_GATHER_MISSING = 1 << 7
FLAGS_INVALID = (FLAG_LIGHTING_MISMATCH | FLAG_TAA_MISMATCH
                 | FLAG_LIGHTING_MISSING | FLAG_TAA_MISSING
                 | FLAG_GATHER_MISMATCH | FLAG_GATHER_MISSING)

FLAG_NAMES = {
    FLAG_TAA_ACTIVE: 'taa_active',
    FLAG_LIGHTING_MISMATCH: 'lighting_stamp_mismatch',
    FLAG_TAA_MISMATCH: 'taa_stamp_mismatch',
    FLAG_LIGHTING_MISSING: 'lighting_stamp_missing',
    FLAG_TAA_MISSING: 'taa_stamp_missing',
    FLAG_RESOLUTION_SPLIT: 'render_output_resolution_split',
    FLAG_GATHER_MISMATCH: 'gather_stamp_mismatch',
    FLAG_GATHER_MISSING: 'gather_stamp_missing',
}

#: channel name -> (field bit, record index inside that field group, meaning of the 4th component)
CHANNELS = {
    'lighting_reflection': (FIELD_LIGHTING, 0, 'roughness (-1 = no geometry)'),
    'lighting_normal':     (FIELD_LIGHTING, 1, 'view distance in cm (-1 = no geometry)'),
    'taa_raw':             (FIELD_TAA, 0, 'final blend weight'),
    'taa_filtered':        (FIELD_TAA, 1, 'velocity.x in output pixels'),
    'taa_history':         (FIELD_TAA, 2, 'velocity.y in output pixels'),
    'taa_clamped':         (FIELD_TAA, 3, '1.0 when the history was rejected as off-screen'),
    'taa_output':          (FIELD_TAA, 4, 'exposure used by the TAA HDR weights'),
    'output':              (FIELD_OUTPUT, 0, 'exposure used by the TAA HDR weights'),
    'taa_clip_min':        (FIELD_TAA_EXTRA, 0, 'centre depth (reversed-Z)'),
    'taa_clip_max':        (FIELD_TAA_EXTRA, 1, 'velocity magnitude in output px'),
    'taa_plus_taps':       (FIELD_TAA_EXTRA, 2, 'plus-filter neighbour lumas (YCoCg Y, 4x): up, left, right, down'),
    'taa_history_sample':  (FIELD_TAA_EXTRA, 3, 'history sample position in pixels (xy) and Catmull-Rom fractions (zw)'),
    'taa_tap_up':          (FIELD_TAA_EXTRA, 4, 'weight actually applied to the up tap (spatial x HDR); rgb = the tap colour'),
    'taa_tap_left':        (FIELD_TAA_EXTRA, 5, 'weight actually applied to the left tap; rgb = the tap colour'),
    'taa_tap_centre':      (FIELD_TAA_EXTRA, 6, 'weight actually applied to the centre tap; rgb = the tap colour'),
    'taa_tap_right':       (FIELD_TAA_EXTRA, 7, 'weight actually applied to the right tap; rgb = the tap colour'),
    'taa_tap_down':        (FIELD_TAA_EXTRA, 8, 'weight actually applied to the down tap; rgb = the tap colour'),
    'taa_filter_params':   (FIELD_TAA_EXTRA, 9, 'w = spatial weight multiplier; xy = jitter offset the filter consumed (px); z = sum of weights'),
    'taa_hdr_params':      (FIELD_TAA_EXTRA, 10, 'w unused; x = exposure scale in HdrWeightY; y = HDR weight exponent; z = reconstruction mode'),
    'direct_light':        (FIELD_LIGHTING_EXTRA, 0, 'shadow visibility (0..1)'),
    'diffuse_indirect':    (FIELD_LIGHTING_EXTRA, 1, 'ambient occlusion'),
    'specular_indirect':   (FIELD_LIGHTING_EXTRA, 2, 'metallic'),
    'emissive_and_mis':    (FIELD_LIGHTING_EXTRA, 3, 'active-path bitmask (float bits of a uint)'),
    'ssgi_consumed':       (FIELD_LIGHTING_EXTRA, 4, 'SSGI/gather mode consumed: 0 none, 1 SDF world gather, 2 legacy screen-space'),
    'translucent_source':  (FIELD_LIGHTING_EXTRA, 5, 'opacity; -1 = the translucent pass did not draw this pixel this frame'),
    'translucent_albedo':  (FIELD_LIGHTING_EXTRA, 6, 'albedo SRV index (float bits of a uint)'),
    'translucent_ibl':     (FIELD_LIGHTING_EXTRA, 7, 'irradiance SRV index (float bits of a uint)'),
    'translucent_normal':  (FIELD_LIGHTING_EXTRA, 8, 'shading-model id (float bits of a uint)'),
    'translucent_emission': (FIELD_LIGHTING_EXTRA, 9, 'N.L'),
    'translucent_layers':  (FIELD_LIGHTING_EXTRA, 10, 'x = translucent layers that passed the depth test here this frame (every rasterised layer with r.Translucency.DepthTest=0); y = depth of the last layer; z = the opaque depth at this pixel (raw reversed-Z, from the deferred pass); can undercount overlap inside one draw call'),
    'rc_support':          (FIELD_LIGHTING_EXTRA, 11, 'w = probes that survived both rules (contributed); x = the cache query\'s surviving weight sum; y = probes with a facing weight > 0; z = probes valid (not buried). All zero when the cache query did not run (a higher-priority source answered)'),
    'ddgi_support':        (FIELD_LIGHTING_EXTRA, 12, 'w = probes with a trilinear weight > 0; x = the DDGI query\'s visibility-weighted weight sum; y = probes with Chebyshev visibility > 1e-3; z = probes with visibility <= 1e-3. All zero when the DDGI query did not run'),
    'gather_current':      (FIELD_GATHER, 0, 'ambient occlusion of the current (post spatial denoise) sample'),
    'gather_history':      (FIELD_GATHER, 1, 'sample count the reprojected history carried'),
    'gather_output':       (FIELD_GATHER, 2, 'the effective alpha the blend used'),
    'gather_state':        (FIELD_GATHER, 3, 'rejection code (see GATHER_REJECT), gather px, gather py, resulting count'),
    'gather_weights':      (FIELD_GATHER, 4, "accumulator bilinear weight sum, history AO, output AO, r.SSGI.TemporalAlpha"),
    'gather_upsample_w':   (FIELD_GATHER, 5, 'the four bilateral upsample tap weights, order (0,0) (1,0) (0,1) (1,1)'),
    'gather_upsample_map': (FIELD_GATHER, 6, 'upsample base x, base y, fraction x, fraction y'),
    'gather_resolution':   (FIELD_GATHER, 7, 'gather width, gather height, full width, full height'),
    'gather_upsampled':    (FIELD_GATHER, 8, 'upsample state (see GATHER_UPSAMPLE)'),
    'gather_world_pos':    (FIELD_GATHER, 9, 'depth at the position the gather resolved'),
    'gather_origin':       (FIELD_GATHER, 10, '1.0 when ResolveSdfRayOrigin succeeded, 0.0 when it did not'),
    'gather_geo_normal':   (FIELD_GATHER, 11, 'hash seed of the sample sequence (float bits of a uint)'),
    'gather_first_ray':    (FIELD_GATHER, 12, 'sample count (r.GI.SDF.DiffuseSamples)'),
    'gather_srv_a':        (FIELD_GATHER, 13, 'frame index, clipmap SRVs 0/1/2 (all float bits of uints)'),
    'gather_srv_b':        (FIELD_GATHER, 14, 'clipmap SRV 3, probe SRV, sky SRV, raw clipmap SRV 0'),
    'gather_probe_origin': (FIELD_GATHER, 15, 'the snap grid cell size in world units'),
    'gather_probe':        (FIELD_GATHER, 16, '1.0 when the jitter-independent probe was written'),
    'gather_accum_srv':    (FIELD_GATHER, 17, 'history SRV, history-geometry SRV, output UAV, history-valid'),
    'gather_probe_volume': (FIELD_GATHER, 18, '1.0 when the snapped probe ray hit geometry, 0.0 when it reached sky'),
    'gather_ray_0':        (FIELD_GATHER, 19, "ray 0: direction (xyz) + its radiance contribution as luminance"),
    'gather_ray_1':        (FIELD_GATHER, 20, 'ray 1: direction (xyz) + its radiance contribution as luminance'),
    'gather_ray_2':        (FIELD_GATHER, 21, 'ray 2: direction (xyz) + its radiance contribution as luminance'),
    'gather_ray_3':        (FIELD_GATHER, 22, 'ray 3: direction (xyz) + its radiance contribution as luminance'),
    'gather_ray_4':        (FIELD_GATHER, 23, 'ray 4: direction (xyz) + its radiance contribution as luminance'),
    'gather_ray_5':        (FIELD_GATHER, 24, 'ray 5: direction (xyz) + its radiance contribution as luminance'),
    'gather_ray_6':        (FIELD_GATHER, 25, 'ray 6: direction (xyz) + its radiance contribution as luminance'),
    'gather_ray_7':        (FIELD_GATHER, 26, 'ray 7: direction (xyz) + its radiance contribution as luminance'),
    'gather_trace':        (FIELD_GATHER, 27, 'raw trace output before the spatial denoise (rgb) + rays recorded'),
    'gather_rotation':     (FIELD_GATHER, 28, 'frame rotation applied (xy) + mode (see GATHER_ROTATION)'),
    'gather_cascade':      (FIELD_GATHER, 29, 'w = fraction of rays escaped; x = fraction of hits inside clipmap 0, y = fraction in a coarser cascade, z = min hit distance to a clipmap-0 face in voxels (negative outside)'),
    'gather_clipmap0':     (FIELD_GATHER, 30, 'w = clipmap 0 half-extent (cm); xyz = its centre as the shader saw it'),
}

#: `gather_reject` codes — an explicit reason, because a count of 1 cannot tell a rejected
#: history from an initialised one.
GATHER_REJECT = {
    0: 'accepted', 1: 'sky_depth', 2: 'history_invalid', 3: 'behind_camera',
    4: 'reprojected_offscreen', 5: 'zero_weight_sum', 6: 'field_changed',
}

#: `gather_upsampled` state codes — an all-zero weight vector alone does not say whether the
#: reconstruction found no compatible tap or the pixel was sky.
GATHER_UPSAMPLE = {0: 'bilateral', 1: 'sky', 2: 'world_gather_fallback', 3: 'no_tap'}

#: `gather_rotation` modes — `r.GI.FinalGather.SampleRotation`. Mode 0 is the shipping path, whose
#: rotation is locked to frame parity (ledger H-31); mode 1 is the pre-registered candidate.
GATHER_ROTATION = {0: 'radical_inverse_shipping', 1: 'kronecker_candidate',
                   2: 'independent_reference_only'}

#: bits of the `emissive_and_mis` alpha channel — which lighting path the pixel actually took
PATH_BITS = {
    1: 'sdf_reflections', 2: 'sdf_diffuse_gather', 4: 'screen_space_ssgi', 8: 'brixelizer_gi',
    16: 'environment_mis', 32: 'aerial_perspective', 64: 'glass_transmission', 128: 'ssr_blended',
}

#: Rec.709 linear luminance weights. The TAA shader works in a 4x-scaled YCoCg Y
#: (Y = R + 2G + B); never mix the two — see docs/analysis known issues.
REC709 = (0.2126, 0.7152, 0.0722)
YCOCG_Y = (0.25, 0.5, 0.25)


#: Record counts per field group, in field-bit order, for every layout that has been written.
#: Format 4 carries its own layout in the header, so this table only serves formats 2 and 3, which
#: did not. Those are told apart by the header's own `recordsPerPixel`; a total matching none of
#: them, or several that disagree on a group the file carries, is rejected rather than guessed at.
LEGACY_LAYOUTS = (
    (2, 5, 1, 3, 4, 0),   # up to 2026-09-09
    (2, 5, 1, 4, 4, 0),   # 2026-09-10, after the TAA history sample position was added
    (2, 5, 1, 4, 5, 0),   # 2026-09-10, after the consumed SSGI value was added
)
#: What the current build writes. Mirrors `runtime/engine/renderer/frame/region_dump_layout.h` and
#: `shaders/runtime/common/region_dump.hlsli`; `scripts/rendering/test_region_format.py` checks all three
#: against a fixture with distinct values per channel, not against each other.
CURRENT_LAYOUT = (2, 5, 1, 11, 13, 31)
MAX_SUPPORTED_VERSION = 5
FIELD_ORDER = (FIELD_LIGHTING, FIELD_TAA, FIELD_OUTPUT, FIELD_TAA_EXTRA,
               FIELD_LIGHTING_EXTRA, FIELD_GATHER)


def unpack_layout(packed: int, version: int) -> tuple:
    """Header word -> record count per group, in field-bit order.

    Format 4 packed six bits per group across five groups; format 5 packs five bits across six,
    because a sixth group no longer fits. The unpacker is version-specific on purpose: guessing
    which packing a file used is the mistake this whole mechanism exists to prevent.
    """
    if version >= 5:
        return tuple((packed >> (i * 5)) & 0x1f for i in range(6))
    return tuple((packed >> (i * 6)) & 0x3f for i in range(5)) + (0,)


def resolve_layout(version: int, fields: int, records: int, packed: int) -> tuple:
    """The record count of each group for one file, taken from the file wherever possible.

    Format 4 states it outright. Formats 2 and 3 do not, so the layout is recovered by matching the
    header's `recordsPerPixel` against the layouts in use while that format was written, and only
    accepted when every layout that matches agrees on the groups the file carries (the legacy layouts
    differ only in TAA_EXTRA and LIGHTING_EXTRA, so a file without those groups matches all three and
    reads the same under each). An unknown, newer format is rejected: a reader that assumes
    the layout has not changed is how every LIGHTING_EXTRA channel came to be read one slot early.
    """
    if version > MAX_SUPPORTED_VERSION:
        raise ValueError(
            f'region dump format {version} is newer than this reader (supports up to '
            f'{MAX_SUPPORTED_VERSION}); update scripts/rendering/flicker/regiondump.py')
    if version >= 4:
        layout = unpack_layout(packed, version)
        total = sum(c for i, c in enumerate(layout) if fields & (1 << i))
        if total != records:
            raise ValueError(f'header layout {layout} sums to {total} for fields={fields}, but the '
                             f'header says {records} records per pixel')
        return layout
    matches = [c for c in LEGACY_LAYOUTS
               if sum(n for i, n in enumerate(c) if fields & (1 << i)) == records]
    carried = {tuple(n for i, n in enumerate(c) if fields & (1 << i)) for c in matches}
    if len(carried) != 1:
        raise ValueError(
            f'format {version} carries no layout, and fields={fields} with {records} records per '
            f'pixel matches {len(matches)} known historical layouts — cannot be read unambiguously')
    return matches[0]


def field_offsets(fields: int, layout: tuple = CURRENT_LAYOUT) -> dict:
    """Record offset of each field group inside a pixel's records, for a given layout."""
    off, out = 0, {}
    for i, bit in enumerate(FIELD_ORDER):
        if fields & bit:
            out[bit] = off
            off += layout[i]
    return out


def records_per_pixel(fields: int, layout: tuple = CURRENT_LAYOUT) -> int:
    """Total records per pixel for a field mask under a given layout."""
    return sum(n for i, n in enumerate(layout) if fields & (1 << i))


def flag_names(flags: int) -> list:
    return [name for bit, name in FLAG_NAMES.items() if flags & bit]


class RectView:
    """One rectangle of one frame."""

    def __init__(self, frame: 'RegionFrame', index: int, name: str, x: int, y: int, w: int, h: int,
                 base: int, lighting_stamp: int, taa_stamp: int, stamp_flags: int):
        self.frame = frame
        self.index = index
        self.name = name
        self.x, self.y, self.w, self.h = x, y, w, h
        self.base = base
        self.lighting_stamp = lighting_stamp
        self.taa_stamp = taa_stamp
        self.stamp_flags = stamp_flags

    @property
    def pixel_count(self) -> int:
        return self.w * self.h

    @property
    def valid(self) -> bool:
        return (self.stamp_flags & FLAGS_INVALID) == 0

    def records(self) -> np.ndarray:
        """(h, w, recordsPerPixel, 4) float32 view of this rectangle's pixel records."""
        r = self.frame.records_per_pixel
        start = self.base
        stop = start + self.pixel_count * r
        return self.frame.data[start:stop].reshape(self.h, self.w, r, 4)

    def channel(self, name: str) -> np.ndarray:
        """(h, w, 4) float32 view of one record: rgb plus the component documented in CHANNELS."""
        field, rec, _ = CHANNELS[name]
        if not (self.frame.fields & field):
            raise KeyError(f"channel '{name}' needs field bit {field}; file has fields={self.frame.fields}")
        # A record past the group's size in THIS file belongs to the next group or the next pixel, so a
        # channel added after the file was written must raise instead of reading a neighbour's data.
        count = self.frame.layout[FIELD_ORDER.index(field)]
        if rec >= count:
            raise KeyError(f"channel '{name}' is record {rec} of its group, but this file's group has "
                           f"{count} records; the file was written before the channel existed")
        # Offsets resolved from this file's own header, not from a constant in this module.
        return self.records()[:, :, self.frame.offsets[field] + rec, :]

    def luminance(self, name: str, coeffs=REC709) -> np.ndarray:
        """(h, w) linear luminance of a channel's RGB. Rec.709 by default."""
        rgb = self.channel(name)[..., :3]
        return rgb @ np.asarray(coeffs, dtype=np.float32)

    def path_mask(self) -> np.ndarray:
        """(h, w) uint32 of the lighting path bits actually taken (see PATH_BITS)."""
        return self.channel('emissive_and_mis')[..., 3].copy().view(np.uint32)

    def path_summary(self) -> dict:
        """Fraction of the rectangle's pixels that took each lighting path."""
        bits = self.path_mask()
        return {name: float(((bits & bit) != 0).mean()) for bit, name in PATH_BITS.items()}

    def geometry_mask(self) -> np.ndarray:
        """(h, w) bool: True where the deferred lighting pass reported geometry this frame."""
        return self.channel('lighting_normal')[..., 3] >= 0.0

    def history_valid_mask(self) -> np.ndarray:
        """(h, w) bool: True where the TAA resolve kept the reprojected history (not off-screen)."""
        return self.channel('taa_clamped')[..., 3] < 0.5

    @property
    def gather_stamp(self) -> int:
        """Frame the SSGI accumulation stamped, from footer record 2; -1 when it never wrote."""
        footer = self.base + self.pixel_count * self.frame.records_per_pixel
        rec = self.frame.data[footer + 2]
        if float(rec[3]) != 1.0:
            return -1
        return int(rec[0].view(np.uint32))

    def gather_reject(self) -> np.ndarray:
        """(h, w) uint32 rejection code of the gather's temporal accumulation (see GATHER_REJECT)."""
        return self.channel('gather_state')[..., 0].copy().view(np.uint32)

    def gather_upsample_state(self) -> np.ndarray:
        """(h, w) uint32 state of the bilateral upsample (see GATHER_UPSAMPLE)."""
        return self.channel('gather_upsampled')[..., 3].copy().view(np.uint32)

    def gather_pixel(self) -> np.ndarray:
        """(h, w, 2) int32 gather pixel that records 0-4 describe — tap (0,0) of the upsample."""
        return self.channel('gather_state')[..., 1:3].astype(np.int32)


class RegionFrame:
    """One region-dump file."""

    def __init__(self, path: str):
        with open(path, 'rb') as f:
            raw = f.read()
        self.path = path
        magic = struct.unpack_from('<I', raw, 0)[0]
        if magic == MAGIC_V1:
            raise ValueError(f'{path}: format version 1 (no frame stamps) is not accepted; recapture with the current build')
        if magic != MAGIC_V2:
            raise ValueError(f'{path}: not a region dump (magic 0x{magic:08x})')
        (_, self.version, self.frame, self.rect_count, self.fields, self.records_per_pixel,
         self.flags, self.render_width, self.render_height, self.output_width, self.output_height,
         self.jitter_index) = struct.unpack_from('<12I', raw, 0)
        self.jitter_x, self.jitter_y, self.exposure = struct.unpack_from('<3f', raw, 48)
        self.group_records = struct.unpack_from('<I', raw, 60)[0]
        # The layout comes from the file, so a capture is always read the way it was written.
        self.layout = resolve_layout(self.version, self.fields, self.records_per_pixel,
                                     self.group_records)
        self.offsets = field_offsets(self.fields, self.layout)

        off = 64
        self.view_proj = self.prev_view_proj = self.camera_pos = None
        if self.version >= 3:
            m = np.frombuffer(raw, dtype=np.float32, count=36, offset=off)
            self.view_proj = m[0:16].reshape(4, 4).copy()
            self.prev_view_proj = m[16:32].reshape(4, 4).copy()
            self.camera_pos = m[32:35].copy()
            off += 144
        rects = []
        for _ in range(self.rect_count):
            name = struct.unpack_from('<16s', raw, off)[0].split(b'\0')[0].decode('ascii', 'replace')
            rects.append((name,) + struct.unpack_from('<4I', raw, off + 16))
            off += 32
        stamps = []
        for _ in range(self.rect_count):
            stamps.append(struct.unpack_from('<4I', raw, off))
            off += 16
        # A truncated file must be rejected, not read as if it held fewer pixels: the record
        # array is indexed by rect base offsets, so a short file silently returns other rects'
        # data or runs off the end. Caught by scripts/rendering/test_region_format.py.
        expected_records = sum(w * h * self.records_per_pixel + 4 for _, _, _, w, h in rects)
        available = (len(raw) - off) // 16
        if available < expected_records:
            raise ValueError(
                f'{path}: truncated — {available} records after the headers, but the rect table '
                f'needs {expected_records} ({expected_records - available} missing)')
        self.data = np.frombuffer(raw, dtype=np.float32, offset=off, count=available * 4).reshape(-1, 4)
        self.rects = [RectView(self, i, name, x, y, w, h, stamps[i][3], stamps[i][0], stamps[i][1], stamps[i][2])
                      for i, (name, x, y, w, h) in enumerate(rects)]

    @property
    def valid(self) -> bool:
        """False when any GPU frame stamp disagreed with the frame the CPU read back."""
        return (self.flags & FLAGS_INVALID) == 0

    def world_positions(self, rect_name: str) -> np.ndarray:
        """(h, w, 3) world position of every pixel of a rectangle, from its recorded view distance.

        The lighting records store the distance from the camera to the shaded point; combined with
        the pixel's ray direction (from the inverse of the recorded unjittered view-projection) that
        gives the world point, which is what lets an analysis follow the same surface while the
        camera moves instead of comparing screen pixels.
        """
        if self.view_proj is None:
            raise ValueError(f'{self.path}: version {self.version} has no camera block; recapture')
        r = self.rect(rect_name)
        dist = r.channel('lighting_normal')[..., 3].astype(np.float64)
        xs = (np.arange(r.w) + r.x + 0.5) / self.output_width * 2.0 - 1.0
        ys = 1.0 - (np.arange(r.h) + r.y + 0.5) / self.output_height * 2.0
        gx, gy = np.meshgrid(xs, ys)
        inv = np.linalg.inv(self.view_proj.astype(np.float64))

        def unproject(z):
            clip = np.stack([gx, gy, np.full_like(gx, z), np.ones_like(gx)], axis=-1).reshape(-1, 4)
            w = clip @ inv
            return (w[:, :3] / w[:, 3:4]).reshape(r.h, r.w, 3)

        # Unprojecting at a single mid depth lands only centimetres from the camera under the
        # engine's reversed-Z projection, so the direction taken from there is dominated by
        # rounding. Two well separated depths give the ray from the matrix alone, and the sign is
        # taken from whichever of the two lies further from the camera, so the reconstruction does
        # not assume a depth convention.
        cam = self.camera_pos.astype(np.float64)
        a, b = unproject(0.9), unproject(0.02)
        dirs = b - a
        dirs /= np.maximum(np.linalg.norm(dirs, axis=-1, keepdims=True), 1e-12)
        da = np.linalg.norm(a - cam, axis=-1, keepdims=True)
        db = np.linalg.norm(b - cam, axis=-1, keepdims=True)
        farther = np.where(da > db, a, b)
        ahead = np.einsum('...i,...i->...', dirs, farther - cam)[..., None]
        dirs = np.where(ahead < 0.0, -dirs, dirs)
        return cam + dirs * dist[..., None]

    def project(self, world: np.ndarray, previous: bool = False) -> np.ndarray:
        """Projects world points to screen pixels using this frame's (or the previous) matrices."""
        m = self.prev_view_proj if previous else self.view_proj
        if m is None:
            raise ValueError(f'{self.path}: version {self.version} has no camera block; recapture')
        w = np.concatenate([world, np.ones(world.shape[:-1] + (1,))], axis=-1)
        clip = w @ m.astype(np.float64)
        wcomp = clip[..., 3:4]
        ndc = clip[..., :3] / np.where(np.abs(wcomp) < 1e-12, 1e-12, wcomp)
        px = (ndc[..., 0] * 0.5 + 0.5) * self.output_width - 0.5
        py = (0.5 - ndc[..., 1] * 0.5) * self.output_height - 0.5
        return np.stack([px, py], axis=-1)

    @property
    def taa_active(self) -> bool:
        return bool(self.flags & FLAG_TAA_ACTIVE)

    def rect(self, name: str) -> RectView:
        for r in self.rects:
            if r.name == name:
                return r
        raise KeyError(f"{self.path}: no rectangle named '{name}' (have {[r.name for r in self.rects]})")

    def __repr__(self):
        return (f'<RegionFrame {os.path.basename(self.path)} frame={self.frame} '
                f'rects={[r.name for r in self.rects]} fields={self.fields} '
                f'jitter=({self.jitter_x:+.3f},{self.jitter_y:+.3f}) idx={self.jitter_index} '
                f'exposure={self.exposure:.4f} valid={self.valid}>')


def load_frame(path: str) -> RegionFrame:
    return RegionFrame(path)


def sequence_paths(directory: str, first: int = None, last: int = None):
    out = []
    for p in glob.glob(os.path.join(directory, 'region_*.bin')):
        m = re.search(r'region_(\d+)\.bin$', os.path.basename(p))
        if not m:
            continue
        n = int(m.group(1))
        if (first is None or n >= first) and (last is None or n <= last):
            out.append((n, p))
    out.sort()
    return out


def check_sequence(directory: str, first: int = None, last: int = None,
                   allow_taa_missing: bool = False) -> dict:
    """Frame-integrity report for a capture: numbers, gaps, duplicates and invalid stamps.

    Returns a dict; `ok` is True only when the range is contiguous, unique, complete and every
    GPU stamp matched. Reading the report is cheaper than loading every frame's pixels.

    `allow_taa_missing` tolerates a missing TAA stamp, and ONLY that. It exists for captures where
    TAA is deliberately disabled as a control (`r.TAA=0`), where no TAA pass runs and so no TAA
    stamp can be written. Never set it to work around an unexplained failure: a missing stamp with
    TAA on means the records and the frame do not belong together.
    """
    paths = sequence_paths(directory, first, last)
    numbers, invalid, mismatched = [], [], []
    for n, p in paths:
        fr = RegionFrame(p)
        numbers.append(fr.frame)
        if fr.frame != n:
            mismatched.append((n, fr.frame))
        if (fr.flags & (FLAGS_INVALID & ~FLAG_TAA_MISSING
                        if allow_taa_missing else FLAGS_INVALID)):
            invalid.append((fr.frame, flag_names(fr.flags)))
    expected = list(range(numbers[0], numbers[0] + len(numbers))) if numbers else []
    missing = sorted(set(expected) - set(numbers)) if numbers else []
    duplicates = sorted({n for n in numbers if numbers.count(n) > 1})
    if first is not None and numbers and numbers[0] != first:
        missing = sorted(set(range(first, numbers[0])) | set(missing))
    if last is not None and numbers and numbers[-1] != last:
        missing = sorted(set(range(numbers[-1] + 1, last + 1)) | set(missing))
    return {
        'directory': directory,
        'count': len(numbers),
        'first': numbers[0] if numbers else None,
        'last': numbers[-1] if numbers else None,
        'missing': missing,
        'duplicates': duplicates,
        'name_mismatch': mismatched,
        'invalid': invalid,
        'ok': bool(numbers) and not missing and not duplicates and not mismatched and not invalid,
        'allow_taa_missing': allow_taa_missing,
    }


def load_sequence(directory: str, first: int = None, last: int = None, require_valid: bool = True,
                  allow_taa_missing: bool = False):
    """Loads a contiguous sequence of frames, raising on gaps, duplicates or invalid stamps.

    Frame integrity is enforced at load time on purpose: a missing or repeated frame silently
    changes every temporal metric, so a caller cannot opt out by forgetting to check.

    `allow_taa_missing` tolerates a missing TAA stamp and nothing else; see `check_sequence`.
    """
    paths = sequence_paths(directory, first, last)
    if not paths:
        raise FileNotFoundError(f'{directory}: no region_*.bin in range {first}..{last}')
    frames = [load_frame(p) for _, p in paths]
    numbers = [f.frame for f in frames]
    if len(set(numbers)) != len(numbers):
        raise ValueError(f'{directory}: duplicate frame numbers')
    expected = list(range(numbers[0], numbers[0] + len(numbers)))
    if numbers != expected:
        raise ValueError(f'{directory}: frame sequence has gaps, missing {sorted(set(expected) - set(numbers))}')
    for f, (n, p) in zip(frames, paths):
        if f.frame != n:
            raise ValueError(f'{p}: header frame {f.frame} disagrees with the file name')
    if require_valid:
        mask = FLAGS_INVALID & ~FLAG_TAA_MISSING if allow_taa_missing else FLAGS_INVALID
        bad = [(f.frame, flag_names(f.flags)) for f in frames if (f.flags & mask)]
        if bad:
            raise ValueError(f'{directory}: {len(bad)} frames have invalid GPU stamps: {bad[:5]}')
    return frames
