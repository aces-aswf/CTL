///////////////////////////////////////////////////////////////////////////
// Copyright Contributors to the CTL project.
// SPDX-License-Identifier: BSD-3-Clause
///////////////////////////////////////////////////////////////////////////

#include <CtlSimdReg.h>
#include <CtlSimdAddr.h>
#include <CtlSimdArena.h>
#include <testSimdRegAddr.h>
#include <testRequire.h>

#include <iostream>
#include <sstream>
#include <vector>
#include <cstdint>
#include <cstring>

using namespace Ctl;
using namespace std;


namespace {

void
testSimdRegHeapValueCtor ()
{
    cout << "  heap value ctor zeroes varying lanes" << endl;

    SimdReg r (true, sizeof(float));
    for (int i = 0; i < MAX_REG_SIZE; ++i)
    {
	float lane;
	memcpy(&lane, r[i], sizeof(float));
	REQUIRE(lane == 0.0f);
    }

    // Write a distinctive value to every lane, read it back.
    for (int i = 0; i < MAX_REG_SIZE; ++i)
    {
	float v = static_cast<float>(i) * 0.5f - 1.0f;
	memcpy(r[i], &v, sizeof(float));
    }
    for (int i = 0; i < MAX_REG_SIZE; ++i)
    {
	float lane;
	memcpy(&lane, r[i], sizeof(float));
	REQUIRE(lane == static_cast<float>(i) * 0.5f - 1.0f);
    }
}


void
testSimdRegSetVaryingTransitions ()
{
    cout << "  setVarying preserves lane[0] across widen + narrow" << endl;

    // Start non-varying with a known value.
    SimdReg r (false, sizeof(float));
    float seed = 3.14159f;
    memcpy(r[0], &seed, sizeof(float));

    // Widen: every lane should equal the original.
    r.setVarying(true);
    for (int i = 0; i < MAX_REG_SIZE; ++i)
    {
	float v;
	memcpy(&v, r[i], sizeof(float));
	REQUIRE(v == seed);
    }

    // Mutate lane 0, narrow back to non-varying -- value preserved.
    float other = -42.0f;
    memcpy(r[0], &other, sizeof(float));
    r.setVarying(false);
    float final;
    memcpy(&final, r[0], sizeof(float));
    REQUIRE(final == other);
}


void
testSimdRegArenaBacked ()
{
    cout << "  arena-backed register has correct ownership flags" << endl;

    SimdArena arena;

    // zeroInit=true (default) -- buffer must be zeroed.
    SimdReg r1 (true, sizeof(float), arena);
    for (int i = 0; i < 64; ++i)
    {
	float v;
	memcpy(&v, r1[i], sizeof(float));
	REQUIRE(v == 0.0f);
    }

    // zeroInit=false -- caller is responsible; just verify writeback works.
    SimdReg r2 (true, sizeof(float), arena, /*zeroInit=*/false);
    for (int i = 0; i < 64; ++i)
    {
	float v = static_cast<float>(i);
	memcpy(r2[i], &v, sizeof(float));
    }
    for (int i = 0; i < 64; ++i)
    {
	float v;
	memcpy(&v, r2[i], sizeof(float));
	REQUIRE(v == static_cast<float>(i));
    }
}


void
testSimdRegCreateInArena ()
{
    cout << "  createInArena allocates object + buffer in arena" << endl;

    SimdArena arena;
    SimdReg *r = SimdReg::createInArena(arena, /*varying=*/true,
					sizeof(float), /*zeroInit=*/true);
    REQUIRE(r != nullptr);
    REQUIRE(r->isArenaOwned());

    // Smoke-test the buffer.
    float v = 99.0f;
    memcpy((*r)[0], &v, sizeof(float));
    float back;
    memcpy(&back, (*r)[0], sizeof(float));
    REQUIRE(back == 99.0f);

    SimdReg::destroy(r);   // arena-aware teardown -- must not call operator delete.
}


void
testSimdRegStructMemberReference ()
{
    cout << "  struct-member reference register writes through to owner" << endl;

    // Owner reg holds a 16-byte "struct" per lane.  Reference register
    // points at offset 8 within each lane (a "second member" of the struct).
    const size_t structSize = 16;
    const size_t memberOffset = 8;
    SimdReg owner (true, structSize);

    // Non-varying offset is the typical struct member access.
    SimdBoolMask mask (false);   // non-varying mask, all lanes active
    SimdReg ref (owner, mask, memberOffset, MAX_REG_SIZE);

    REQUIRE(ref.isReference());

    // Write distinct values through the reference at lanes 0, 1, 7, 100.
    int lanes[] = {0, 1, 7, 100};
    uint32_t values[] = {0xDEADBEEFu, 0xCAFEBABEu, 0x12345678u, 0x0F0F0F0Fu};
    for (int k = 0; k < 4; ++k)
    {
	memcpy(ref[lanes[k]], &values[k], sizeof(uint32_t));
    }

    // Read them back from the OWNER reg at the corresponding offsets.
    for (int k = 0; k < 4; ++k)
    {
	uint32_t got;
	memcpy(&got, owner[lanes[k]] + memberOffset, sizeof(uint32_t));
	REQUIRE(got == values[k]);
    }
}


void
testSimdBoolMaskBasics ()
{
    cout << "  SimdBoolMask non-varying / varying / setVarying" << endl;

    // Non-varying: inline scalar storage.
    SimdBoolMask m (false);
    REQUIRE(!m.isVarying());
    m[0] = true;
    REQUIRE(m[5] == true);   // non-varying: any index reads lane[0]
    m[0] = false;
    REQUIRE(m[123] == false);

    // Promote to varying: every lane should equal the prior scalar value.
    m[0] = true;
    m.setVarying(true);
    REQUIRE(m.isVarying());
    for (int i = 0; i < 32; ++i)
	REQUIRE(m[i] == true);

    // Mutate lane 5; lane 0 should still be true; lane 5 false.
    m[5] = false;
    REQUIRE(m[0] == true);
    REQUIRE(m[5] == false);

    // Narrow: lane[0] preserved.
    m.setVarying(false);
    REQUIRE(!m.isVarying());
    REQUIRE(m[0] == true);

    // Stronger lane[0]-vs-lane[1] discrimination check:
    // mutation testing surfaced that the previous narrow-preserve check
    // could pass even if setVarying(false) preserved a different lane.
    // Set lane[0] explicitly different from lane[1] before narrow, then
    // verify the inline scalar holds lane[0]'s value, not lane[1]'s.
    m.setVarying(true);
    for (int i = 0; i < 16; ++i) m[i] = true;
    m[0] = false;            // lane[0] must differ from lane[1]
    REQUIRE(m[0] == false);
    REQUIRE(m[1] == true);
    m.setVarying(false);
    REQUIRE(m[0] == false);  // narrow MUST preserve lane[0], not lane[1]
}


void
testSimdBoolMaskCopyAndPoolExhaustion ()
{
    cout << "  SimdBoolMask copy ctor + pool reuse beyond kCacheMax" << endl;

    SimdBoolMask src (true);
    for (int i = 0; i < 10; ++i)
	src[i] = (i & 1) != 0;

    SimdBoolMask dst (src, /*copyLen=*/10);
    REQUIRE(dst.isVarying());
    for (int i = 0; i < 10; ++i)
	REQUIRE(dst[i] == ((i & 1) != 0));

    // Allocate well past detail::BoolBufferPool::kCacheMax (32) -- the
    // overflow falls through to heap alloc/free.  Just constructing
    // and destructing them in stack order exercises both pool reuse
    // and overflow.
    {
	std::vector<SimdBoolMask*> masks;
	masks.reserve(64);
	for (int i = 0; i < 64; ++i)
	{
	    SimdBoolMask *p = new SimdBoolMask(true);
	    p[0][0] = ((i & 1) == 0);
	    masks.push_back(p);
	}
	for (int i = 63; i >= 0; --i)
	{
	    REQUIRE((*masks[i])[0] == ((i & 1) == 0));
	    delete masks[i];
	}
    }

    // After the burst, a fresh allocation should still work and be writable.
    SimdBoolMask after (true);
    after[0] = true;
    REQUIRE(after[0] == true);
}


void
testSimdArenaAlignmentAndReset ()
{
    cout << "  SimdArena alignment, reset reuse, multi-chunk growth" << endl;

    SimdArena arena;

    // Several modest allocations: every returned pointer is 16-byte aligned.
    char *p1 = arena.allocate(13);
    char *p2 = arena.allocate(1);
    char *p3 = arena.allocate(64);
    REQUIRE(p1 != nullptr && p2 != nullptr && p3 != nullptr);
    REQUIRE((reinterpret_cast<uintptr_t>(p1) & 15) == 0);
    REQUIRE((reinterpret_cast<uintptr_t>(p2) & 15) == 0);
    REQUIRE((reinterpret_cast<uintptr_t>(p3) & 15) == 0);
    // Each subsequent allocation begins after the previous (rounded up
    // to alignment): p2 should be at least 16 bytes past p1.
    REQUIRE(p2 >= p1 + 16);
    REQUIRE(p3 >= p2 + 16);

    // Reset, then re-allocate -- should reuse chunk 0 (returns the same
    // base pointer for the first allocation).
    arena.reset();
    char *q = arena.allocate(13);
    REQUIRE(q == p1);

    // Force a multi-chunk growth: allocate something larger than the
    // initial chunk's remaining space.  The arena adds a new chunk;
    // the existing pointer (q) must remain valid.
    arena.reset();
    char *first = arena.allocate(16);
    char *huge  = arena.allocate(4 * 1024 * 1024);   // 4 MiB > 2 MiB chunk
    REQUIRE(huge != nullptr);
    // Original pointer still readable/writable after growth.
    first[0] = 'X';
    REQUIRE(first[0] == 'X');
}


void
testSimdDataAddr ()
{
    cout << "  SimdDataAddr two ctors + copy + assign + print branches" << endl;

    // Absolute (reg) ctor.
    SimdReg backing (false, sizeof(float));
    SimdDataAddr abs(&backing);
    REQUIRE(abs.reg() == &backing);

    // Frame-pointer-relative ctor.
    SimdDataAddr fpRel(/*fpOffset=*/-32);
    REQUIRE(fpRel.reg() == nullptr);   // relative addrs return null from no-arg reg()

    // Copy ctor -- preserves both branches.
    SimdDataAddr absCopy(abs);
    REQUIRE(absCopy.reg() == &backing);

    SimdDataAddr fpCopy(fpRel);
    REQUIRE(fpCopy.reg() == nullptr);

    // operator= -- both branches.
    SimdDataAddr a(&backing);
    a = fpRel;
    REQUIRE(a.reg() == nullptr);
    a = abs;
    REQUIRE(a.reg() == &backing);

    // print(): rebind cout to a stringstream so we can run both branches
    // without polluting the test output.
    std::ostringstream capture;
    std::streambuf *prev = std::cout.rdbuf(capture.rdbuf());
    abs.print(2);
    fpRel.print(4);
    std::cout.rdbuf(prev);
    const std::string out = capture.str();
    REQUIRE(out.find("reg addr") != std::string::npos);
    REQUIRE(out.find("reg fp offset -32") != std::string::npos);
}

} // anonymous namespace


void
testSimdRegAddr ()
{
    cout << endl;
    cout << "Testing SimdReg / SimdBoolMask / SimdArena / SimdDataAddr"
	 << endl;

    testSimdRegHeapValueCtor();
    testSimdRegSetVaryingTransitions();
    testSimdRegArenaBacked();
    testSimdRegCreateInArena();
    testSimdRegStructMemberReference();
    testSimdBoolMaskBasics();
    testSimdBoolMaskCopyAndPoolExhaustion();
    testSimdArenaAlignmentAndReset();
    testSimdDataAddr();

    cout << "ok" << endl;
}
