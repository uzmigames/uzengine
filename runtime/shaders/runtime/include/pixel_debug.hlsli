/// UzEngine PixelDebug — GPU printf for shader diagnostics.
///
/// @ref NVIDIA Falcor PixelDebug pattern — InterlockedAdd + structured buffer readback.
///
/// Each PDEBUG() reads UAV indices directly from the cbuffer struct fields
/// (no intermediate locals) to prevent DXC from optimizing away UAV writes.

#ifndef PIXEL_DEBUG_HLSLI
#define PIXEL_DEBUG_HLSLI

#ifdef PIXEL_DEBUG_ENABLED

/// Declares local variables for PDEBUG. Copies values from the cbuffer struct
/// into locals that are passed through to UAV writes.
#define PDEBUG_DECL(px, cb)                       \
    uint _pd_ctrI = (cb).pdCounterUavIdx;          \
    uint _pd_recI = (cb).pdRecordsUavIdx;          \
    uint _pd_mxR  = (cb).pdMaxRecords;              \
    uint _pd_dX   = (cb).pdPixelX;                  \
    uint _pd_dY   = (cb).pdPixelY;                  \
    uint _pd_pX   = (px).x;                         \
    uint _pd_pY   = (px).y;

/// Write a float debug value directly to the GPU buffer.
/// `cb` must be the same struct variable passed to PDEBUG_DECL (e.g., gBufIndices).
/// Reads UAV indices and debug pixel from cbuffer fields each time — no caching.
/// Write a float debug value. Uses local variables declared by PDEBUG_DECL.
#define PDEBUG(id, val) {                                                  \
    [branch] if (_pd_pX == _pd_dX && _pd_pY == _pd_dY) {                  \
        RWByteAddressBuffer _c = ResourceDescriptorHeap[_pd_ctrI];         \
        RWByteAddressBuffer _r = ResourceDescriptorHeap[_pd_recI];         \
        uint _s; _c.InterlockedAdd(0, 1u, _s);                            \
        [branch] if (_s < _pd_mxR) {                                       \
            uint _o = _s * 16;                                             \
            _r.Store(_o,      (uint)(id));                                 \
            _r.Store(_o + 4,  asuint((float)(val)));                       \
            _r.Store(_o + 8,  _pd_dX);                                     \
            _r.Store(_o + 12, _pd_dY);                                     \
        }                                                                  \
    }                                                                      \
}
#define PDEBUG_U(id, val)       PDEBUG(id, val)
#define PDEBUG_VEC3(id, v)      { PDEBUG(id, (v).x); PDEBUG((id)+1, (v).y); PDEBUG((id)+2, (v).z); }
#define PDEBUG_VEC4(id, v)      { PDEBUG(id, (v).x); PDEBUG((id)+1, (v).y); PDEBUG((id)+2, (v).z); PDEBUG((id)+3, (v).w); }

// Compat aliases
#define PDEBUG_CB(cb, id, val)  PDEBUG(id, val)
#define PDEBUG_SET(id, val)     PDEBUG(id, val)
#define PDEBUG_FLUSH()
#define PDEBUG_INIT(pixel, counterIdx, recordsIdx, debugPixel, maxRecords)

#else

#define PDEBUG_DECL(px, cbName)
#define PDEBUG_CB(cb, id, val)
#define PDEBUG(id, val)
#define PDEBUG_U(id, val)
#define PDEBUG_SET(id, val)
#define PDEBUG_FLUSH()
#define PDEBUG_VEC3(id, v)
#define PDEBUG_VEC4(id, v)
#define PDEBUG_INIT(pixel, counterIdx, recordsIdx, debugPixel, maxRecords)

#endif
#endif
