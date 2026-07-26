///////////////////////////////////////////////////////////////////////////
// Copyright (c) 2013 Academy of Motion Picture Arts and Sciences
// ("A.M.P.A.S."). Portions contributed by others as indicated.
// All rights reserved.
//
// (License text omitted for brevity -- see CtlSimdReg.h for the full
// ASWF BSD-style license that governs this file.)
///////////////////////////////////////////////////////////////////////////


#ifndef INCLUDED_CTL_SIMD_ARENA_H
#define INCLUDED_CTL_SIMD_ARENA_H

//-----------------------------------------------------------------------------
//
//	class SimdArena -- per-xcontext bump allocator for varying SimdReg
//	data buffers.
//
//	One arena per SimdXContext (i.e. per thread on the tile-parallel
//	path).  Per-instruction varying SimdReg buffers (16 KB for a float
//	lane x 4096-lane varying register) are allocated from the arena
//	instead of from libc malloc, and the arena is reset at the end of
//	each callFunction() run.
//
//	Motivation: profiling showed ~8% of wall-clock in libc allocator
//	plumbing and ~1.3% in the zero-fill branch of `new char[...]()` on
//	aces_combined x 4 Mpx.  Arena allocation eliminates both costs for
//	the hot per-instruction varying-reg path.
//
//	The arena is a growable chunked bump allocator: one chunk holds
//	INITIAL_CHUNK_BYTES (2 MiB) of scratch space, which is enough for
//	peak stack depth on aces_combined (~1 MiB observed).  If a single
//	callFunction exhausts the chunk, a second chunk is appended (never
//	reallocated -- existing pointers stay valid); reset() simply rewinds
//	the tip to the start of chunk 0 and releases any extra chunks on
//	the next callFunction.
//
//-----------------------------------------------------------------------------

#include <cstddef>
#include <cstdlib>
#include <cstdint>
#include <vector>

namespace Ctl {


class SimdArena
{
  public:

     SimdArena ();
    ~SimdArena ();

    // Allocate `bytes` of memory, aligned to kAlignment (16 B).
    // The returned pointer is stable until the next reset(), even if
    // the arena grows to accommodate a later allocation.
    // Does NOT zero-initialize -- callers rely on overwriting the
    // whole buffer themselves, which is the common path.
    char *	allocate (std::size_t bytes);

    // Rewind the arena to its initial state so the next allocate()
    // starts from offset 0 in chunk 0.  Does not free chunks; they
    // are reused across callFunction invocations.
    void	reset ();

  private:

    // Non-copyable -- an arena is owned by exactly one SimdXContext.
    SimdArena (const SimdArena &);
    SimdArena &operator= (const SimdArena &);

    static const std::size_t kAlignment = 16;
    static const std::size_t kInitialChunkBytes = 2 * 1024 * 1024;

    struct Chunk
    {
	// malloc aligns only for max_align_t, which is 8 bytes on 32-bit
	// targets such as armv7 and 16 on x86_64 and arm64.  Chunks are
	// over-allocated so base can be rounded up to kAlignment; raw is the
	// pointer that has to go back to free().
	char *		raw;
	char *		base;
	std::size_t	capacity;
    };

    std::vector<Chunk>	_chunks;
    std::size_t		_chunkIdx;	// current chunk
    std::size_t		_offset;	// bump offset within current chunk

    bool	addChunk (std::size_t minBytes);
};


inline char *
SimdArena::allocate (std::size_t bytes)
{
    // Round bytes up to kAlignment so every allocation starts aligned.
    const std::size_t aligned = (bytes + kAlignment - 1) & ~(kAlignment - 1);

    if (_chunkIdx < _chunks.size() &&
	_offset + aligned <= _chunks[_chunkIdx].capacity)
    {
	char *p = _chunks[_chunkIdx].base + _offset;
	_offset += aligned;
	return p;
    }

    // Slow path: advance to next chunk or grow.
    if (_chunkIdx + 1 < _chunks.size() &&
	aligned <= _chunks[_chunkIdx + 1].capacity)
    {
	++_chunkIdx;
	_offset = aligned;
	return _chunks[_chunkIdx].base;
    }

    if (!addChunk (aligned))
	return 0;	// allocation failure (caller falls back to heap)

    _chunkIdx = _chunks.size() - 1;
    _offset = aligned;
    return _chunks[_chunkIdx].base;
}


} // namespace Ctl

#endif // INCLUDED_CTL_SIMD_ARENA_H
