///////////////////////////////////////////////////////////////////////////
// Copyright Contributors to the CTL project.
// SPDX-License-Identifier: BSD-3-Clause
///////////////////////////////////////////////////////////////////////////

#include <CtlSimdReg.h>
#include <testSimdRegOwnership.h>
#include <testRequire.h>

#include <iostream>
#include <cstring>
#include <cstdint>

using namespace Ctl;
using namespace std;


namespace {

void
fillSentinel (SimdReg &r, uint32_t base)
{
    for (int i = 0; i < MAX_REG_SIZE; ++i)
    {
	const uint32_t v = base + static_cast<uint32_t>(i);
	memcpy(r[i], &v, sizeof(uint32_t));
    }
}


void
testNonTransferReferenceLifetimes ()
{
    cout << "  state 1 + state 2: ref into static survives owner up to its dtor"
	 << endl;

    SimdReg owner (true, sizeof(uint32_t));
    fillSentinel(owner, 0xCAFE0000);

    {
	SimdBoolMask mask (false);
	SimdReg ref (owner, mask, /*offset=*/0, MAX_REG_SIZE);
	REQUIRE(ref.isReference());
	REQUIRE(!ref.isArenaOwned());

	for (int i = 0; i < MAX_REG_SIZE; i += 1024)
	{
	    uint32_t got;
	    memcpy(&got, ref[i], sizeof(uint32_t));
	    REQUIRE(got == 0xCAFE0000u + static_cast<uint32_t>(i));
	}

	uint32_t marker = 0xDEAFBEEF;
	memcpy(ref[42], &marker, sizeof(uint32_t));
	uint32_t echo;
	memcpy(&echo, owner[42], sizeof(uint32_t));
	REQUIRE(echo == 0xDEAFBEEFu);
    }

    // ref destruction above must not delete owner's buffer.
    uint32_t survivor;
    memcpy(&survivor, owner[100], sizeof(uint32_t));
    REQUIRE(survivor == 0xCAFE0000u + 100u);
}


void
testTransferDataMakesOriginalSafelyDestructible ()
{
    cout << "  state 1 + transferData=true: original becomes state 4, both safe to dtor"
	 << endl;

    // After transferData=true, ref owns the buffer (state 3) and owner
    // is state 4 (_data=null).  Destroy ref first, which frees the buffer;
    // destroy owner second, so delete[] on its now-null _data is a no-op.
    SimdReg *owner = new SimdReg (true, sizeof(uint32_t));
    fillSentinel(*owner, 0x1234ABCD);

    SimdBoolMask mask (false);
    SimdReg *ref = new SimdReg (*owner, mask, /*offset=*/0,
				MAX_REG_SIZE, /*transferData=*/true);
    REQUIRE(ref->isReference());

    uint32_t got;
    memcpy(&got, (*ref)[7], sizeof(uint32_t));
    REQUIRE(got == 0x1234ABCDu + 7u);

    delete ref;
    delete owner;
}


void
testTransferDataReverseDestructionOrder ()
{
    cout << "  state 4 then state 3: destroying owner first must not poison ref"
	 << endl;

    // Reverse destruction order: destroying owner (now state 4) first
    // must leave ref's buffer intact.
    SimdReg *owner = new SimdReg (true, sizeof(uint32_t));
    fillSentinel(*owner, 0x55AA00FF);

    SimdBoolMask mask (false);
    SimdReg *ref = new SimdReg (*owner, mask, /*offset=*/0,
				MAX_REG_SIZE, /*transferData=*/true);

    delete owner;

    uint32_t got;
    memcpy(&got, (*ref)[15], sizeof(uint32_t));
    REQUIRE(got == 0x55AA00FFu + 15u);

    uint32_t mark = 0xFFFFFFFF;
    memcpy((*ref)[15], &mark, sizeof(uint32_t));
    memcpy(&got, (*ref)[15], sizeof(uint32_t));
    REQUIRE(got == 0xFFFFFFFFu);

    delete ref;
}


void
testReferenceMethodTransfersData ()
{
    cout << "  reference() member: in-place transition to state 3 via transferData"
	 << endl;

    SimdReg source (true, sizeof(uint32_t));
    fillSentinel(source, 0xAABBCCDD);

    SimdReg target (true, sizeof(uint32_t));
    target.reference(source, /*transferData=*/true);

    REQUIRE(target.isReference());

    uint32_t got;
    memcpy(&got, target[3], sizeof(uint32_t));
    REQUIRE(got == 0xAABBCCDDu + 3u);

    // source destructor (at scope exit) must be safe despite state 4.
}


void
testValueRegBasicReadWrite ()
{
    cout << "  state 1 isolated: value reg owns and frees its own buffer"
	 << endl;

    // 16 alloc+free cycles of a 64 KiB buffer (8192 lanes * 8B).
    // A dtor leak shows up under ASan/valgrind, not here.
    for (int iter = 0; iter < 16; ++iter)
    {
	SimdReg r (true, sizeof(uint64_t));
	uint64_t marker = 0xDEADBEEFCAFEBABEull;
	memcpy(r[0],          &marker, sizeof(uint64_t));
	memcpy(r[MAX_REG_SIZE - 1], &marker, sizeof(uint64_t));

	uint64_t echo0, echoN;
	memcpy(&echo0, r[0], sizeof(uint64_t));
	memcpy(&echoN, r[MAX_REG_SIZE - 1], sizeof(uint64_t));
	REQUIRE(echo0 == marker);
	REQUIRE(echoN == marker);
    }
}

} // anonymous namespace


void
testSimdRegOwnership ()
{
    cout << endl;
    cout << "Testing SimdReg ownership state transitions" << endl;

    testValueRegBasicReadWrite();
    testNonTransferReferenceLifetimes();
    testTransferDataMakesOriginalSafelyDestructible();
    testTransferDataReverseDestructionOrder();
    testReferenceMethodTransfersData();

    cout << "ok" << endl;
}
