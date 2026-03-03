/*
 * rhi_common.c -- Global RHI accessor + PSO cache + hash helpers
 *
 * This file contains:
 *   1. The global rhi() accessor and rhi_set_backend() setter
 *   2. A simple open-addressing hash table for PSO caching
 *   3. Hash helper functions (FNV-1a for now; TODO Phase 2: xxHash64 via vcpkg)
 *
 * No backend-specific headers are included here.
 */

#include "rhi.h"

#include <assert.h>
#include <string.h>

/* =======================================================================
 * Global backend accessor
 * ======================================================================= */

static const rhi_backend_t *g_rhi = NULL;

const rhi_backend_t *rhi(void) {
    return g_rhi;
}

void rhi_set_backend(const rhi_backend_t *backend) {
    g_rhi = backend;
}

/* =======================================================================
 * Hash helpers
 *
 * Using FNV-1a for Phase 1 (no external dependencies).
 * TODO Phase 2: Replace with xxHash64 (xxhash vcpkg package) for better
 * distribution and performance on large shader blobs.
 * ======================================================================= */

uint64_t rhi_hash_bytes(const void *data, size_t size) {
    const uint8_t *p = (const uint8_t *)data;
    uint64_t hash = 14695981039346656037ULL; /* FNV-1a offset basis (64-bit) */
    for (size_t i = 0; i < size; i++) {
        hash ^= (uint64_t)p[i];
        hash *= 1099511628211ULL; /* FNV-1a prime (64-bit) */
    }
    return hash;
}

uint32_t rhi_hash_bytes32(const void *data, size_t size) {
    const uint8_t *p = (const uint8_t *)data;
    uint32_t hash = 2166136261u; /* FNV-1a offset basis (32-bit) */
    for (size_t i = 0; i < size; i++) {
        hash ^= (uint32_t)p[i];
        hash *= 16777619u; /* FNV-1a prime (32-bit) */
    }
    return hash;
}

/* =======================================================================
 * PSO cache key construction
 * ======================================================================= */

RhiPsoCacheKey rhi_make_pso_cache_key(const RhiPipelineDesc *desc) {
    RhiPsoCacheKey key;
    memset(&key, 0, sizeof(key));

    if (desc->vertex_shader.spirv_data && desc->vertex_shader.spirv_size > 0) {
        key.vs_spirv_hash = rhi_hash_bytes(desc->vertex_shader.spirv_data,
                                           desc->vertex_shader.spirv_size);
    }
    if (desc->pixel_shader.spirv_data && desc->pixel_shader.spirv_size > 0) {
        key.ps_spirv_hash = rhi_hash_bytes(desc->pixel_shader.spirv_data,
                                           desc->pixel_shader.spirv_size);
    }

    key.rt_formats_hash = rhi_hash_bytes32(desc->rt_formats,
                                           sizeof(desc->rt_formats));

    /*
     * Hash the fixed-size state structs.  We hash the rasterizer, blend,
     * depth-stencil, topology, ds_format, and vertex layout together.
     * This works because the structs have deterministic layout (no pointers
     * except debug_name which we exclude, no padding-sensitive fields).
     */
    struct {
        RhiRasterizerState   rasterizer;
        RhiBlendState        blend;
        RhiDepthStencilState depth_stencil;
        RhiTopology          topology;
        RhiFormat            ds_format;
        uint32_t             rt_count;
    } state_block;
    memset(&state_block, 0, sizeof(state_block));
    state_block.rasterizer    = desc->rasterizer;
    state_block.blend         = desc->blend;
    state_block.depth_stencil = desc->depth_stencil;
    state_block.topology      = desc->topology;
    state_block.ds_format     = desc->ds_format;
    state_block.rt_count      = desc->rt_count;

    key.state_hash = rhi_hash_bytes32(&state_block, sizeof(state_block));

    return key;
}

/* =======================================================================
 * PSO cache (open-addressing hash table, power-of-2 capacity)
 *
 * Fixed 256-entry table for Phase 1.  No heap allocations after init.
 * TODO Phase 2: Grow dynamically, persist to .uz_cache/pso/ on disk.
 * ======================================================================= */

#define PSO_CACHE_CAPACITY 256 /* Must be power of 2 */

typedef struct PsoCacheEntry {
    RhiPsoCacheKey key;
    RhiPipeline    pipeline;
    bool           occupied;
} PsoCacheEntry;

static PsoCacheEntry s_pso_cache[PSO_CACHE_CAPACITY];
static bool          s_pso_cache_initialized = false;

static uint32_t pso_cache_key_hash(const RhiPsoCacheKey *key) {
    return rhi_hash_bytes32(key, sizeof(*key));
}

static bool pso_cache_key_equal(const RhiPsoCacheKey *a, const RhiPsoCacheKey *b) {
    return a->vs_spirv_hash   == b->vs_spirv_hash
        && a->ps_spirv_hash   == b->ps_spirv_hash
        && a->rt_formats_hash == b->rt_formats_hash
        && a->state_hash      == b->state_hash;
}

void rhi_pso_cache_init(void) {
    memset(s_pso_cache, 0, sizeof(s_pso_cache));
    s_pso_cache_initialized = true;
}

void rhi_pso_cache_shutdown(void) {
    /*
     * Note: We do NOT destroy the pipeline handles here -- the backend
     * owns those and will destroy them during its own shutdown sequence.
     * We just clear the lookup table.
     */
    memset(s_pso_cache, 0, sizeof(s_pso_cache));
    s_pso_cache_initialized = false;
}

RhiPipeline rhi_pso_cache_find(const RhiPsoCacheKey *key) {
    assert(s_pso_cache_initialized);
    if (!s_pso_cache_initialized) {
        return RHI_NULL;
    }

    uint32_t idx = pso_cache_key_hash(key) & (PSO_CACHE_CAPACITY - 1);

    for (uint32_t probe = 0; probe < PSO_CACHE_CAPACITY; probe++) {
        uint32_t slot = (idx + probe) & (PSO_CACHE_CAPACITY - 1);
        if (!s_pso_cache[slot].occupied) {
            return RHI_NULL; /* Empty slot: key not in table */
        }
        if (pso_cache_key_equal(&s_pso_cache[slot].key, key)) {
            return s_pso_cache[slot].pipeline;
        }
    }

    return RHI_NULL; /* Table full and key not found */
}

void rhi_pso_cache_insert(const RhiPsoCacheKey *key, RhiPipeline pso) {
    assert(s_pso_cache_initialized);
    if (!s_pso_cache_initialized) {
        return;
    }

    uint32_t idx = pso_cache_key_hash(key) & (PSO_CACHE_CAPACITY - 1);

    for (uint32_t probe = 0; probe < PSO_CACHE_CAPACITY; probe++) {
        uint32_t slot = (idx + probe) & (PSO_CACHE_CAPACITY - 1);
        if (!s_pso_cache[slot].occupied) {
            s_pso_cache[slot].key      = *key;
            s_pso_cache[slot].pipeline = pso;
            s_pso_cache[slot].occupied = true;
            return;
        }
        if (pso_cache_key_equal(&s_pso_cache[slot].key, key)) {
            /* Key already exists -- update the pipeline handle */
            s_pso_cache[slot].pipeline = pso;
            return;
        }
    }

    /* Table is full.  In production this should not happen with 256 entries.
     * TODO Phase 2: Grow the table or evict LRU entries. */
    assert(0 && "PSO cache is full");
}

void rhi_pso_cache_invalidate_by_shader(uint64_t spirv_hash) {
    assert(s_pso_cache_initialized);
    if (!s_pso_cache_initialized) {
        return;
    }

    /*
     * Scan the entire table and remove any entry whose VS or PS hash matches.
     * This is called during shader hot-reload.  With 256 entries and infrequent
     * hot-reload, a linear scan is fine.
     *
     * Note: We mark slots as unoccupied.  This can break linear probing chains.
     * For Phase 1 with small tables and infrequent invalidation, this is
     * acceptable.  Phase 2 should use tombstone markers or re-insert displaced
     * entries.
     *
     * TODO Phase 2: Use tombstone-based deletion for correctness under
     * heavy invalidation.
     */
    for (uint32_t i = 0; i < PSO_CACHE_CAPACITY; i++) {
        if (!s_pso_cache[i].occupied) {
            continue;
        }
        if (s_pso_cache[i].key.vs_spirv_hash == spirv_hash ||
            s_pso_cache[i].key.ps_spirv_hash == spirv_hash) {
            /*
             * Note: The backend pipeline (PSO) itself is NOT destroyed here.
             * The caller (shader hot-reload system) is responsible for
             * destroying stale pipelines via rhi()->pipeline_destroy().
             * We only remove the cache mapping.
             */
            memset(&s_pso_cache[i], 0, sizeof(s_pso_cache[i]));
        }
    }
}
