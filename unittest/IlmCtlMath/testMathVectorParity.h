///////////////////////////////////////////////////////////////////////////
// Copyright Contributors to the CTL project.
// SPDX-License-Identifier: BSD-3-Clause
///////////////////////////////////////////////////////////////////////////

#ifndef INCLUDED_CTL_TEST_MATH_VECTOR_PARITY_H
#define INCLUDED_CTL_TEST_MATH_VECTOR_PARITY_H

//
// Validates that the vectorized transcendental backend compiled into
// IlmCtlSimd matches scalar libm to within sleef/Accelerate's published
// <=1 ULP contract across every function on the batched hot path.
//
// Runs a dense random sweep per function, counts ULP drift per sample,
// asserts >=99.9% of samples within 1 ULP, and logs each function's
// observed max-ULP to stderr so per-platform / per-backend variation
// is visible in CI output.
//
// In the default build (CTL_USE_SLEEF=OFF and CTL_USE_ACCELERATE=OFF)
// the test prints a skip note and returns without error -- there is
// no vectorized entry point to validate and the scalar libm path is
// bit-identical with itself by construction.
//

void testMathVectorParity ();

#endif
