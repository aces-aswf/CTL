///////////////////////////////////////////////////////////////////////////
// Copyright (c) 2013 Academy of Motion Picture Arts and Sciences
// ("A.M.P.A.S."). Portions contributed by others as indicated.
// All rights reserved.
//
// (License text omitted for brevity -- see CtlSimdReg.h for the full
// ASWF BSD-style license that governs this file.)
///////////////////////////////////////////////////////////////////////////

#include <CtlSimdArena.h>

namespace Ctl {


SimdArena::SimdArena ()
    : _chunks(), _chunkIdx(0), _offset(0)
{
    addChunk (kInitialChunkBytes);
}


SimdArena::~SimdArena ()
{
    for (std::size_t i = 0; i < _chunks.size(); ++i)
	std::free (_chunks[i].raw);
}


void
SimdArena::reset ()
{
    _chunkIdx = 0;
    _offset = 0;
}


bool
SimdArena::addChunk (std::size_t minBytes)
{
    std::size_t cap = kInitialChunkBytes;
    while (cap < minBytes)
	cap *= 2;

    // Over-allocate so the usable base can be rounded up to kAlignment.
    // allocate() keeps every offset a multiple of kAlignment, so aligning the
    // base is what makes each returned pointer aligned.  malloc alone is not
    // enough: it only promises max_align_t, which is 8 bytes on armv7.
    void *mem = std::malloc (cap + kAlignment - 1);
    if (!mem)
	return false;

    const std::uintptr_t addr = reinterpret_cast<std::uintptr_t>(mem);
    const std::uintptr_t base =
	(addr + kAlignment - 1) & ~static_cast<std::uintptr_t>(kAlignment - 1);

    Chunk c;
    c.raw = static_cast<char *>(mem);
    c.base = reinterpret_cast<char *>(base);
    c.capacity = cap;
    _chunks.push_back (c);
    return true;
}


} // namespace Ctl
