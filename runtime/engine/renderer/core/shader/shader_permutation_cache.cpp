/// UzEngine Shader Permutation Cache -- implementation.

#include "shader_permutation_cache.h"

#include "log.h"

namespace uz {

// ---------------------------------------------------------------------------
// Lookup
// ---------------------------------------------------------------------------

RHIPipelineHandle ShaderPermutationCache::Lookup(const ShaderPermutationKey& key) const
{
    const uint64_t hash = key.GetHash();
    const auto it = m_cache.find(hash);

    if (it == m_cache.end()) {
        return RHIPipelineHandle::Invalid();
    }

    // Verify the full key matches to guard against hash collisions.
    if (it->second.key != key) {
        UZ_LOG(Renderer, Warning,
               "ShaderPermutationCache: hash collision detected for key '{}' vs cached '{}'",
               key.ToString(), it->second.key.ToString());
        return RHIPipelineHandle::Invalid();
    }

    return it->second.pipeline;
}

// ---------------------------------------------------------------------------
// Insert
// ---------------------------------------------------------------------------

void ShaderPermutationCache::Insert(const ShaderPermutationKey& key, RHIPipelineHandle pipeline)
{
    const uint64_t hash = key.GetHash();
    m_cache[hash] = CacheEntry{key, pipeline};
}

// ---------------------------------------------------------------------------
// GetOrCreatePSO
// ---------------------------------------------------------------------------

RHIPipelineHandle ShaderPermutationCache::GetOrCreatePSO(
    const ShaderPermutationKey& key,
    [[maybe_unused]] IRHIDevice* device,
    [[maybe_unused]] ShaderManager* shaderMgr)
{
    const RHIPipelineHandle cached = Lookup(key);
    if (cached.IsValid()) {
        return cached;
    }

    // Cache miss -- PSO creation will be integrated in task 0.11 when the
    // PipelineLibrary is connected to the permutation system. For now, log
    // the miss so the caller knows the permutation needs to be pre-warmed.
    UZ_LOG(Renderer, Warning,
           "ShaderPermutationCache: miss for permutation '{}' (raw=0x{:04X}). "
           "Pre-warm the cache via Insert() until task 0.11 connects PipelineLibrary.",
           key.ToString(), key.GetRawBits());

    return RHIPipelineHandle::Invalid();
}

// ---------------------------------------------------------------------------
// InvalidateAll
// ---------------------------------------------------------------------------

void ShaderPermutationCache::InvalidateAll()
{
    const uint32_t count = static_cast<uint32_t>(m_cache.size());
    m_cache.clear();

    if (count > 0) {
        UZ_LOG(Renderer, Info,
               "ShaderPermutationCache: invalidated {} cached permutation(s)", count);
    }
}

// ---------------------------------------------------------------------------
// GetEntryCount
// ---------------------------------------------------------------------------

uint32_t ShaderPermutationCache::GetEntryCount() const
{
    return static_cast<uint32_t>(m_cache.size());
}

} // namespace uz
